//
// Address.cc
//
// Copyright (c) 2017 Couchbase, Inc All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "Address.hh"
#include "Error.hh"
#include "StringUtil.hh"

using namespace std;
using namespace fleece;

namespace litecore { namespace websocket {

    Address::Address(const string &scheme_, const string &hostname_,
                     uint16_t port_, const string &path_)
    :scheme(scheme_)
    ,hostname(hostname_)
    ,port(port_ ? port_ : defaultPort())
    ,path(path_)
    { }


    bool Address::isSecure() const {
        return (scheme == "wss" || scheme == "https" || scheme == "blips");
    }

    uint16_t Address::defaultPort() const {
        return isSecure() ? 443 : 80;
    }

    Address::operator string() const {
        stringstream result;
        result << scheme << ':' << hostname;
        if (port != defaultPort())
            result << ':' << port;
        if (path.empty() || path[0] != '/')
            result << '/';
        result << path;
        return result.str();
    }


    bool Address::domainEquals(const std::string &d1, const std::string &d2) {
        return compareIgnoringCase(d1, d2);
    }

    bool Address::domainContains(const string &baseDomain, const string &hostname) {
        return hasSuffixIgnoringCase(hostname, baseDomain)
        && (hostname.size() == baseDomain.size()
            || hostname[hostname.size() - baseDomain.size() - 1] == '.');
    }

    bool Address::pathContains(const string &basePath, const string &path) {
        if (basePath.empty())
            return true;
        if (path.empty())
            return false;
        return hasPrefix(path, basePath)
        && (path.size() == basePath.size()
            || path[basePath.size()] == '/'
            || basePath[basePath.size()-1] == '/');
    }


} }
