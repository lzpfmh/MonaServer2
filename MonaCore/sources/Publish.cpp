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

#include "Mona/Publish.h"

using namespace std;


namespace Mona {

Publish::Publish(ServerAPI& api, const char* name) : _pPublishing(new Publishing(api, name)) {
	api.queue(_pPublishing);
}
 
Publish::~Publish() {
	struct Unpublishing : Action, virtual Object {
		Unpublishing(const shared<Publishing>& pPublishing) : Action("Unpublishing", pPublishing) {}
	private:
		void run(Publication& publication) { api().unpublish(publication); }
	};
	queue<Unpublishing>();
}

bool Publish::reset() {
	struct Reset : Action, virtual Object {
		Reset(const shared<Publishing>& pPublishing) : Action("Publish::Reset", pPublishing) {}
	private:
		void run(Publication& publication) { publication.reset(); }
	};
	return queue<Reset>();
}

bool Publish::flush(UInt16 ping) {
	struct Flush : Action, virtual Object {
		Flush(const shared<Publishing>& pPublishing, UInt16 ping) : Action("Publish::Flush", pPublishing), _ping(ping) {}
	private:
		void run(Publication& publication) { publication.flush(_ping); }
		UInt16			_ping;
	};
	return queue<Flush>(ping);
}


} // namespace Mona
