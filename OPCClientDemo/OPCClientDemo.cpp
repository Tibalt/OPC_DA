// OPCClientDemo.cpp : Defines the entry point for the console application.
//

/*
OPCClientToolKit
Copyright (C) 2005 Mark C. Beharrell

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with this library; if not, write to the
Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA  02111-1307, USA.
*/

#include <stdio.h>
#include "opcda.h"
#include "OPCClient.h"
#include "OPCHost.h"
#include "OPCServer.h"
#include "OPCGroup.h"
#include "OPCItem.h"
#include <sys\timeb.h>



/**
* Code demonstrates
* 1) Browsing local servers.
* 2) Connection to local server.
* 3) Browsing local server namespace
* 4) Creation of OPC item and group
* 5) synch and asynch read of OPC item.
* 6) synch and asynch write of OPC item.
* 7) The receipt of changes to items within a group (subscribe)
* 8) group refresh.
* 9) Synch read of multiple OPC items.
*/


/**
* Handle asynch data coming from changes in the OPC group
*/
class CMyCallback:public IAsynchDataCallback{
	public:
		void OnDataChange(COPCGroup & group, CAtlMap<COPCItem *, OPCItemData *> & changes){
			printf("Group %s, item changes\n", group.getName());
			POSITION pos = changes.GetStartPosition();
			while (pos != NULL){
				COPCItem * item = changes.GetNextKey(pos);
				printf("-----> %s\n", item->getName());
			}
		}
		
};



/**
*	Handle completion of asynch operation
*/
class CTransComplete:public ITransactionComplete{
	CString completionMessage;
public:
	CTransComplete(){
		completionMessage = "Asynch operation is completed";
	};

	void complete(CTransaction &transaction){
		printf("%s\n",completionMessage);
	}

	void setCompletionMessage(const CString & message){
		completionMessage = message;
	}
};




//---------------------------------------------------------
// main



#define MESSAGEPUMPUNTIL(x)	while(!x){{MSG msg;while(PeekMessage(&msg,NULL,NULL,NULL,PM_REMOVE)){TranslateMessage(&msg);DispatchMessage(&msg);}Sleep(1);}}


void main(void)
{
	COPCClient::init();

	CString hostName = "pcitco89";
	//CString hostName = "192.168.1.3";
	COPCHost *host = COPCClient::makeHost(hostName);
	
	//List servers
	CAtlArray<CString> localServerList;
	host->getListOfDAServers(IID_CATID_OPCDAServer20, localServerList);
	unsigned i = 0;
	for (; i < localServerList.GetCount(); i++){
		printf("%s\n", localServerList[i]);
	}

	// connect to OPC
	CString serverName = "WIENER.Plein.Baus.OPC.Server.DA";//"CAEN.HVOPCServer.1";
	COPCServer *opcServer = host->connectDAServer(serverName);

	// Check status
	ServerStatus status;
	opcServer->getStatus(status);
	printf("Server state is %ld\n", status.dwServerState);

	// browse server
	CAtlArray<CString> opcItemNames;
	opcServer->getItemNames(opcItemNames);
	printf("Got %d names\n", opcItemNames.GetCount());
	for (i = 0; i < opcItemNames.GetCount(); i++){
		printf("%s\n",opcItemNames[i]);
	}

	// make group
	unsigned long refreshRate;
	COPCGroup *group = opcServer->makeGroup("Group",true,1000,refreshRate,0.0);
	CAtlArray<COPCItem *> items;

	// make item
	CString changeChanNameName = "Server.TestObjects.I1_Cache";
	COPCItem * readWritableItem = group->addItem(changeChanNameName, true);

	// make several items
	CAtlArray<CString> itemNames;
	CAtlArray<COPCItem *>itemsCreated;
	CAtlArray<HRESULT> errors;

	itemNames.Add("Server.TestObjects.I2_Cache");
	itemNames.Add("Server.TestObjects.I4_Cache");	
	if (group->addItems(itemNames, itemsCreated,errors,true) != 0){
		printf("Item create failed\n");
	}

	// get properties
	CAtlArray<CPropertyDescription> propDesc;
	readWritableItem->getSupportedProperties(propDesc);
	printf("Supported properties for %s\n", readWritableItem->getName());
	for (i = 0; i < propDesc.GetCount(); i++){
		printf("%d id = %u, description = %s, type = %d\n", i, propDesc[i].id,propDesc[i].desc,propDesc[i].type);
	}

	CAutoPtrArray<SPropertyValue> propVals;
	readWritableItem->getProperties(propDesc, propVals);
	for (i = 0; i < propDesc.GetCount(); i++){
		if (propVals[i] == NULL){
			printf("Failed to get property %u\n", propDesc[i].id);
		}else{
			printf("Property %u=", propDesc[i].id);
			switch (propDesc[i].type){
				case VT_R4:printf("%f\n", propVals[i]->value.fltVal); break;
				case VT_I4:printf("%d\n", propVals[i]->value.iVal); break;
				case VT_I2:printf("%d\n", propVals[i]->value.iVal); break;
				case VT_I1:{int v = propVals[i]->value.bVal;printf("%d\n", v);} break;
				default:printf("\n");break;
			}
		}
	}


	// SYNCH READ of item
	OPCItemData data;
	readWritableItem->readSync(data,OPC_DS_DEVICE);
	printf("Synch read quality %d value %d\n", data.wQuality, data.vDataValue.iVal);

	// SYNCH read on Group
	COPCItem_DataMap opcData;
	group->readSync(itemsCreated,opcData, OPC_DS_DEVICE);

	// Enable asynch - must be done before any asynch call will work
	CMyCallback usrCallBack;
	group->enableAsynch(usrCallBack);
	
	// ASYNCH OPC item READ
	CTransComplete complete;
	complete.setCompletionMessage("*******Asynch read completion handler has been invoked (OPC item)");
	CTransaction* t = readWritableItem->readAsynch(&complete);
	MESSAGEPUMPUNTIL(t->isCompeleted())
	
	const OPCItemData * asychData = t->getItemValue(readWritableItem); // not owned
	if (!FAILED(asychData->error)){
		printf("Asynch read quality %d value %d\n", asychData->wQuality, asychData->vDataValue.iVal);
	}
	delete t;

	// Aysnch read opc items from a group
	complete.setCompletionMessage("*******Asynch read completion handler has been invoked (OPC GROUP)");
	t = group->readAsync(itemsCreated, &complete);
	MESSAGEPUMPUNTIL(t->isCompeleted())
	delete t;
	

	// SYNCH write
	VARIANT var;
	var.vt = VT_I2;
	var.iVal = 99;
	readWritableItem->writeSync(var);


	// ASYNCH write
	var.vt = VT_I2;
	var.iVal = 32;
	complete.setCompletionMessage("*******Asynch write completion handler has been invoked");
	t = readWritableItem->writeAsynch(var, &complete);
	MESSAGEPUMPUNTIL(t->isCompeleted())

	asychData = t->getItemValue(readWritableItem); // not owned
	if (!FAILED(asychData->error)){
		printf("Asynch write comleted OK\n");
	}else{
		printf("Asynch write failed\n");
	}
	delete t;
	

	// Group refresh (asynch operation) - pass results to CMyCallback as well
	complete.setCompletionMessage("*******Refresh completion handler has been invoked");
	t = group->refresh(OPC_DS_DEVICE, /*true,*/ &complete);
	MESSAGEPUMPUNTIL(t->isCompeleted())

	// readWritableItem is member of group - look for this and use it as a guide to see if operation succeeded.
	asychData = t->getItemValue(readWritableItem); 
	if (!FAILED(asychData->error)){
		printf("refresh compeleted OK\n");
	}else{
		printf("refresh failed\n");
	}
	delete t;


	// just loop - changes to Items within a group are picked up here 
	MESSAGEPUMPUNTIL(false)
}