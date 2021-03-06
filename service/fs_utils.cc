/* fs_utils.cc
   Wolfgang Sourdeau, February 2014
   Copyright (c) 2014 Datacratic.  All rights reserved.

   A set of file-system abstraction functions intended to support common
   operations among different fs types or alikes.
*/

#include <memory>
#include <map>
#include <mutex>

#include "boost/filesystem.hpp"
#include "googleurl/src/url_util.h"

#include "fs_utils.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


using namespace std;
using namespace Datacratic;
namespace fs = boost::filesystem;

namespace {

/* registry */

std::mutex registryMutex;
map<string, std::unique_ptr<const UrlFsHandler> > registry;

} // file scope

namespace Datacratic {

/* LOCALURLFSHANDLER */

struct LocalUrlFsHandler : public UrlFsHandler {
    virtual FsObjectInfo getInfo(const Url & url) const
    {
        FsObjectInfo urlInfo;
        struct stat stats;
        string path = url.path();
        int res = ::stat(path.c_str(), &stats);
        if (res == -1) {
            throw ML::Exception(errno, "stat");
        }

        // TODO: owner ID (uid) and name (uname)

        urlInfo.exists = true;
        urlInfo.lastModified = Date::fromTimespec(stats.st_mtim);
        urlInfo.size = stats.st_size;

        return urlInfo;
    }

    virtual void makeDirectory(const Url & url) const
    {
        boost::system::error_code ec;
        string path = url.path();
        if (!fs::exists(path) && !fs::create_directories(path, ec)) {
            throw ML::Exception(ec.message());
        }
    }

    virtual void erase(const Url & url) const
    {
        string path = url.path();
        int res = ::unlink(path.c_str());
        if (res == -1) {
            throw ML::Exception(errno, "unlink");
        }
    }
};

const UrlFsHandler * findFsHandler(const string & scheme)
{
    std::unique_lock<std::mutex> guard(registryMutex);
    auto handler = registry.find(scheme);
    if (handler == registry.end()) {
        throw ML::Exception("no handler found for scheme: " + scheme);
    }
    return handler->second.get();
}

struct AtInit {
    AtInit() {
        registerUrlFsHandler("file", new LocalUrlFsHandler());
    }
} atInit;

/* ensures that local filenames are represented as urls */
Url makeUrl(const string & urlStr)
{
    if (urlStr[0] == '/') {
        return Url("file://" + urlStr);
    }
    else {
        return Url(urlStr);
    }
}

}


namespace Datacratic {

/* URLFSHANDLER */

size_t
UrlFsHandler::
getSize(const Url & url) const
{
    return getInfo(url).size;
}

string
UrlFsHandler::
getEtag(const Url & url) const
{
    return getInfo(url).etag;
}


/* registry */

void registerUrlFsHandler(const std::string & scheme,
                          UrlFsHandler * handler)
{
    if (registry.find(scheme) != registry.end()) {
        throw ML::Exception("fs handler already registered");
    }

    /* this enables googleuri to parse our urls properly */
    url_util::AddStandardScheme(scheme.c_str());

    registry[scheme].reset(handler);
}

FsObjectInfo
getUriObjectInfo(const std::string & url)
{
    Url realUrl = makeUrl(url);
    return findFsHandler(realUrl.scheme())->getInfo(realUrl);
}

FsObjectInfo
tryGetUriObjectInfo(const std::string & url)
{
    JML_TRACE_EXCEPTIONS(false);
    try {
        return getUriObjectInfo(url);
    }
    catch (...) {
        return FsObjectInfo();
    }
}

size_t
getUriSize(const std::string & url)
{
    Url realUrl = makeUrl(url);
    return findFsHandler(realUrl.scheme())->getSize(realUrl);
}

std::string
getUriEtag(const std::string & url)
{
    Url realUrl = makeUrl(url);
    return findFsHandler(realUrl.scheme())->getEtag(realUrl);
}

void
makeUriDirectory(const std::string & url)
{
    Url realUrl = makeUrl(url);
    findFsHandler(realUrl.scheme())->makeDirectory(realUrl);
}

void
eraseUriObject(const std::string & url)
{
    Url realUrl = makeUrl(url);
    findFsHandler(realUrl.scheme())->erase(realUrl);
}

bool
tryEraseUriObject(const std::string & uri)
{
    JML_TRACE_EXCEPTIONS(false);

    bool result(true);
    try {
        eraseUriObject(uri);
    }
    catch (...) {
        result = false;
    }

    return result;
}

} // namespace Datacratic
