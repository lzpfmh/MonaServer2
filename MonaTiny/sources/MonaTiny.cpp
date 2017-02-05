/*
This file is a part of MonaSolutions Copyright 2017
mathieu.poux[a]gmail.com
jammetthomas[a]gmail.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License received along this program for more
details (or else see http://www.gnu.org/licenses/).

*/

#include "Mona/MonaTiny.h"
#include "Mona/Logs.h"

using namespace std;

namespace Mona {


//// External Publish ////



//// Server Events /////
void MonaTiny::onStart() {
	// create applications
	// _applications["/multicast"] = new MulticastApp(*this,poolBuffers);
}

void MonaTiny::manage() {
	// manage application!
	for (auto& it : _applications)
		it.second->manage();
}

void MonaTiny::onStop() {
	
	// delete applications
	for (auto& it : _applications)
		delete it.second;
	_applications.clear();

	// unblock ctrl+c waiting
	_terminateSignal.set();
}

//// Client Events /////

void MonaTiny::onConnection(Exception& ex, Client& client, DataReader& parameters, DataWriter& response) {
	NOTE(client.protocol, " ", client.address, " connects to ", client.path.empty() ? "/" : client.path)
	const auto& it(_applications.find(client.path));
	if (it == _applications.end())
		return;

	client.setCustomData<App::Client>(it->second->newClient(ex,client,parameters,response));
}

void MonaTiny::onDisconnection(Client& client) {
	NOTE(client.protocol, " ", client.address, " disconnects from ", client.path.empty() ? "/" : client.path);
	if (client.hasCustomData()) {
		delete client.getCustomData<App::Client>();
		client.setCustomData<App::Client>(NULL);
	}
}

void MonaTiny::onAddressChanged(Client& client, const SocketAddress& oldAddress) {
	if (client.hasCustomData())
		client.getCustomData<App::Client>()->onAddressChanged(oldAddress);
}

bool MonaTiny::onInvocation(Exception& ex, Client& client, const string& name, DataReader& arguments, UInt8 responseType) {
	// on client message, returns "false" if "name" message is unknown
	DEBUG(name," call from ",client.protocol," to ",client.path.empty() ? "/" : client.path)
	if (client.hasCustomData())
		return client.getCustomData<App::Client>()->onInvocation(ex, name, arguments,responseType);
	return false;
} 


bool MonaTiny::onFileAccess(Exception& ex, File::Mode mode, Path& file, DataReader& arguments, DataWriter& properties, Client* pClient) {
	// on client file access, returns "false" if acess if forbiden
	if(pClient) {
		DEBUG(file.name(), " file access from ", pClient->protocol, " to ", pClient->path.empty() ? "/" : pClient->path);
		if (pClient->hasCustomData())
			return pClient->getCustomData<App::Client>()->onFileAccess(ex, mode, file, arguments, properties);
	} else
		DEBUG(file.name(), " file access to ", file.parent().empty() ? "/" : file.parent());
	// arguments.read(properties); to test HTTP page properties (HTTP parsing!)
	return true;
}


//// Publication Events /////

bool MonaTiny::onPublish(Exception& ex, const Publication& publication, Client* pClient) {
	if (pClient) {
		NOTE("Client publish ", publication.name());
		if (pClient->hasCustomData())
			return pClient->getCustomData<App::Client>()->onPublish(ex, publication);
	} else
		NOTE("Publish ",publication.name())

	return true; // "true" to allow, "false" to forbid
}

void MonaTiny::onUnpublish(const Publication& publication, Client* pClient) {
	if (pClient) {
		NOTE("Client unpublish ", publication.name());
		if(pClient->hasCustomData())
			pClient->getCustomData<App::Client>()->onUnpublish(publication);
	} else
		NOTE("Unpublish ",publication.name())
}

bool MonaTiny::onSubscribe(Exception& ex, const Subscription& subscription, const Publication& publication, Client* pClient) {
	if (pClient) {
	//	_test.start(*publish(ex, publication.name()));
		NOTE(pClient->protocol, " ", pClient->address, " subscribe to ", publication.name());
		if (pClient->hasCustomData())
			return pClient->getCustomData<App::Client>()->onSubscribe(ex, subscription, publication);
	} else
		NOTE("Subscribe to ", publication.name());
	return true; // "true" to allow, "false" to forbid
} 

void MonaTiny::onUnsubscribe(const Subscription& subscription, const Publication& publication, Client* pClient) {
	if (pClient) {
	//	_test.stop();
	//	unpublish((Publication&)publication);
	//	return;
		NOTE(pClient->protocol, " ", pClient->address, " unsubscribe to ", publication.name());
		if (pClient->hasCustomData())
			return pClient->getCustomData<App::Client>()->onUnsubscribe(subscription, publication);
	} else
		NOTE("Unsubscribe to ", publication.name());
}

//// P2P Group Events /////

void MonaTiny::onJoinGroup(Client& client, Group& group) {
	if (client.hasCustomData())
		client.getCustomData<App::Client>()->onJoinGroup(group);
}

void MonaTiny::onUnjoinGroup(Client& client, Group& group) {
	if (client.hasCustomData())
		client.getCustomData<App::Client>()->onUnjoinGroup(group);
}

} // namespace Mona