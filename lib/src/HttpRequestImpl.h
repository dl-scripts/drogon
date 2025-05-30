/**
 *
 *  @file HttpRequestImpl.h
 *  An Tao
 *
 *  Copyright 2018, An Tao.  All rights reserved.
 *  https://github.com/an-tao/drogon
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Drogon
 *
 */

#pragma once

#include "HttpUtils.h"
#include "CacheFile.h"
#include "impl_forwards.h"
#include <drogon/utils/Utilities.h>
#include <drogon/HttpRequest.h>
#include <drogon/RequestStream.h>
#include <drogon/utils/Utilities.h>
#include <trantor/net/EventLoop.h>
#include <trantor/net/InetAddress.h>
#include <trantor/net/Certificate.h>
#include <trantor/utils/Logger.h>
#include <trantor/utils/MsgBuffer.h>
#include <trantor/utils/NonCopyable.h>
#include <trantor/net/TcpConnection.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <future>
#include <unordered_map>
#include <assert.h>
#include <stdio.h>

namespace drogon
{
enum class StreamDecompressStatus
{
    TooLarge,
    DecompressError,
    NotSupported,
    Ok
};

enum class ReqStreamStatus
{
    None = 0,
    Open = 1,
    Finish = 2,
    Error = 3
};

class HttpRequestImpl : public HttpRequest
{
  public:
    friend class HttpRequestParser;

    explicit HttpRequestImpl(trantor::EventLoop *loop)
        : creationDate_(trantor::Date::now()), loop_(loop)
    {
    }

    void reset()
    {
        method_ = Invalid;
        previousMethod_ = Invalid;
        version_ = Version::kUnknown;
        flagForParsingJson_ = false;
        headers_.clear();
        cookies_.clear();
        contentLengthHeaderValue_.reset();
        realContentLength_ = 0;
        flagForParsingParameters_ = false;
        path_.clear();
        originalPath_.clear();
        pathEncode_ = true;
        matchedPathPattern_ = "";
        query_.clear();
        parameters_.clear();
        jsonPtr_.reset();
        sessionPtr_.reset();
        attributesPtr_.reset();
        cacheFilePtr_.reset();
        expectPtr_.reset();
        content_.clear();
        contentType_ = CT_TEXT_PLAIN;
        flagForParsingContentType_ = false;
        contentTypeString_.clear();
        keepAlive_ = true;
        jsonParsingErrorPtr_.reset();
        peerCertificate_.reset();
        routingParams_.clear();
        // stream
        streamStatus_ = ReqStreamStatus::None;
        streamReaderPtr_.reset();
        streamFinishCb_ = nullptr;
        streamExceptionPtr_ = nullptr;
        startProcessing_ = false;
        connPtr_.reset();
    }

    trantor::EventLoop *getLoop()
    {
        return loop_;
    }

    void setVersion(Version v)
    {
        version_ = v;
        if (version_ == Version::kHttp10)
        {
            keepAlive_ = false;
        }
    }

    Version version() const override
    {
        return version_;
    }

    const char *versionString() const override;

    bool setMethod(const char *start, const char *end);

    void setSecure(bool secure)
    {
        isOnSecureConnection_ = secure;
    }

    void setMethod(const HttpMethod method) override
    {
        previousMethod_ = method_;
        method_ = method;
        return;
    }

    HttpMethod method() const override
    {
        return method_;
    }

    bool isHead() const override
    {
        return (method_ == HttpMethod::Head) ||
               ((method_ == HttpMethod::Get) &&
                (previousMethod_ == HttpMethod::Head));
    }

    const char *methodString() const override;

    void setPath(const char *start, const char *end)
    {
        if (utils::needUrlDecoding(start, end))
        {
            originalPath_.append(start, end);
            path_ = utils::urlDecode(start, end);
        }
        else
        {
            path_.append(start, end);
        }
    }

    const std::vector<std::string> &getRoutingParameters() const override
    {
        return routingParams_;
    }

    void setRoutingParameters(std::vector<std::string> &&params) override
    {
        routingParams_ = std::move(params);
    }

    void setPath(const std::string &path) override
    {
        path_ = path;
    }

    void setPath(std::string &&path) override
    {
        path_ = std::move(path);
    }

    void setPathEncode(bool pathEncode) override
    {
        pathEncode_ = pathEncode;
    }

    const SafeStringMap<std::string> &parameters() const override
    {
        parseParametersOnce();
        return parameters_;
    }

    const std::string &getParameter(const std::string &key) const override
    {
        static const std::string defaultVal;
        parseParametersOnce();
        auto iter = parameters_.find(key);
        if (iter != parameters_.end())
            return iter->second;
        return defaultVal;
    }

    const std::string &path() const override
    {
        return path_;
    }

    const std::string &getOriginalPath() const override
    {
        return originalPath_.empty() ? path_ : originalPath_;
    }

    void setQuery(const char *start, const char *end)
    {
        query_.assign(start, end);
    }

    void setQuery(const std::string &query)
    {
        query_ = query;
    }

    std::string_view bodyView() const
    {
        if (isStreamMode())
        {
            return emptySv_;
        }
        if (cacheFilePtr_)
        {
            return cacheFilePtr_->getStringView();
        }
        return content_;
    }

    const char *bodyData() const override
    {
        if (isStreamMode())
        {
            return emptySv_.data();
        }
        if (cacheFilePtr_)
        {
            return cacheFilePtr_->getStringView().data();
        }
        return content_.data();
    }

    size_t bodyLength() const override
    {
        if (isStreamMode())
        {
            return emptySv_.length();
        }
        if (cacheFilePtr_)
        {
            return cacheFilePtr_->getStringView().length();
        }
        return content_.length();
    }

    void appendToBody(const char *data, size_t length);

    void reserveBodySize(size_t length);

    std::string_view queryView() const
    {
        return query_;
    }

    std::string_view contentView() const
    {
        if (isStreamMode())
        {
            return emptySv_;
        }
        if (cacheFilePtr_)
            return cacheFilePtr_->getStringView();
        return content_;
    }

    const std::string &query() const override
    {
        return query_;
    }

    const trantor::InetAddress &peerAddr() const override
    {
        return peer_;
    }

    const trantor::InetAddress &localAddr() const override
    {
        return local_;
    }

    const trantor::Date &creationDate() const override
    {
        return creationDate_;
    }

    const trantor::CertificatePtr &peerCertificate() const override
    {
        return peerCertificate_;
    }

    void setCreationDate(const trantor::Date &date)
    {
        creationDate_ = date;
    }

    void setPeerAddr(const trantor::InetAddress &peer)
    {
        peer_ = peer;
    }

    void setLocalAddr(const trantor::InetAddress &local)
    {
        local_ = local;
    }

    void setPeerCertificate(const trantor::CertificatePtr &cert)
    {
        peerCertificate_ = cert;
    }

    void setConnectionPtr(const std::shared_ptr<trantor::TcpConnection> &ptr)
    {
        connPtr_ = ptr;
    }

    void addHeader(const char *start, const char *colon, const char *end);

    void removeHeader(std::string key) override
    {
        transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return tolower(c);
        });
        removeHeaderBy(key);
    }

    void removeHeaderBy(const std::string &lowerKey)
    {
        headers_.erase(lowerKey);
    }

    const std::string &getHeader(std::string field) const override
    {
        std::transform(field.begin(),
                       field.end(),
                       field.begin(),
                       [](unsigned char c) { return tolower(c); });
        return getHeaderBy(field);
    }

    const std::string &getHeaderBy(const std::string &lowerField) const
    {
        static const std::string defaultVal;
        auto it = headers_.find(lowerField);
        if (it != headers_.end())
        {
            return it->second;
        }
        return defaultVal;
    }

    const std::string &getCookie(const std::string &field) const override
    {
        static const std::string defaultVal;
        auto it = cookies_.find(field);
        if (it != cookies_.end())
        {
            return it->second;
        }
        return defaultVal;
    }

    const SafeStringMap<std::string> &headers() const override
    {
        return headers_;
    }

    const SafeStringMap<std::string> &cookies() const override
    {
        return cookies_;
    }

    std::optional<size_t> getContentLengthHeaderValue() const
    {
        return contentLengthHeaderValue_;
    }

    size_t realContentLength() const override
    {
        return realContentLength_;
    }

    void setParameter(const std::string &key, const std::string &value) override
    {
        flagForParsingParameters_ = true;
        parameters_[key] = value;
    }

    const std::string &getContent() const
    {
        return content_;
    }

    void swap(HttpRequestImpl &that) noexcept;

    void setContent(const std::string &content)
    {
        content_ = content;
    }

    void setBody(const std::string &body) override
    {
        content_ = body;
    }

    void setBody(std::string &&body) override
    {
        content_ = std::move(body);
    }

    void addHeader(std::string field, const std::string &value) override
    {
        transform(field.begin(),
                  field.end(),
                  field.begin(),
                  [](unsigned char c) { return tolower(c); });
        headers_[std::move(field)] = value;
    }

    void addHeader(std::string field, std::string &&value) override
    {
        transform(field.begin(),
                  field.end(),
                  field.begin(),
                  [](unsigned char c) { return tolower(c); });
        headers_[std::move(field)] = std::move(value);
    }

    void addCookie(std::string key, std::string value) override
    {
        cookies_[std::move(key)] = std::move(value);
    }

    void setPassThrough(bool flag) override
    {
        passThrough_ = flag;
    }

    bool passThrough() const
    {
        return passThrough_;
    }

    void appendToBuffer(trantor::MsgBuffer *output) const;

    const SessionPtr &session() const override
    {
        return sessionPtr_;
    }

    void setSession(const SessionPtr &session)
    {
        sessionPtr_ = session;
    }

    const AttributesPtr &attributes() const override
    {
        if (!attributesPtr_)
        {
            attributesPtr_ = std::make_shared<Attributes>();
        }
        return attributesPtr_;
    }

    const std::shared_ptr<Json::Value> &jsonObject() const override
    {
        // Not multi-thread safe but good, because we basically call this
        // function in a single thread
        if (!flagForParsingJson_)
        {
            flagForParsingJson_ = true;
            parseJson();
        }
        return jsonPtr_;
    }

    void setCustomContentTypeString(const std::string &type) override
    {
        contentType_ = CT_NONE;
        flagForParsingContentType_ = true;
        bool haveHeader = type.find("content-type: ") == 0;
        bool haveCRLF = type.rfind("\r\n") == type.size() - 2;

        size_t endOffset = 0;
        if (haveHeader)
            endOffset += 14;
        if (haveCRLF)
            endOffset += 2;
        contentTypeString_ = std::string(type.begin() + (haveHeader ? 14 : 0),
                                         type.end() - endOffset);
    }

    void setContentTypeCode(const ContentType type) override
    {
        contentType_ = type;
        flagForParsingContentType_ = true;
        auto &typeStr = contentTypeToMime(type);
        setContentType(std::string(typeStr.data(), typeStr.length()));
    }

    void setContentTypeString(const char *typeString,
                              size_t typeStringLength) override;

    // void setContentTypeCodeAndCharacterSet(ContentType type, const
    // std::string &charSet = "utf-8") override
    // {
    //     contentType_ = type;
    //     setContentType(webContentTypeAndCharsetToString(type, charSet));
    // }

    ContentType contentType() const override
    {
        parseContentTypeAndString();
        return contentType_;
    }

    const char *matchedPathPatternData() const override
    {
        return matchedPathPattern_.data();
    }

    size_t matchedPathPatternLength() const override
    {
        return matchedPathPattern_.length();
    }

    void setMatchedPathPattern(const std::string &pathPattern)
    {
        matchedPathPattern_ = pathPattern;
    }

    const std::string &expect() const
    {
        static const std::string none{""};
        if (expectPtr_)
            return *expectPtr_;
        return none;
    }

    bool keepAlive() const
    {
        return keepAlive_;
    }

    bool connected() const noexcept override
    {
        if (auto conn = connPtr_.lock())
        {
            return conn->connected();
        }
        return false;
    }

    const std::weak_ptr<trantor::TcpConnection> &getConnectionPtr()
        const noexcept override
    {
        return connPtr_;
    }

    bool isOnSecureConnection() const noexcept override
    {
        return isOnSecureConnection_;
    }

    const std::string &getJsonError() const override
    {
        static const std::string none{""};
        if (jsonParsingErrorPtr_)
            return *jsonParsingErrorPtr_;
        return none;
    }

    StreamDecompressStatus decompressBody();

    // Stream mode api
    ReqStreamStatus streamStatus() const
    {
        return streamStatus_;
    }

    bool isStreamMode() const
    {
        return streamStatus_ > ReqStreamStatus::None;
    }

    void streamStart();
    void streamFinish();
    void streamError(std::exception_ptr ex);

    void setStreamReader(RequestStreamReaderPtr reader);
    void waitForStreamFinish(std::function<void()> &&cb);
    void quitStreamMode();

    void startProcessing()
    {
        startProcessing_ = true;
    }

    bool isProcessingStarted() const
    {
        return startProcessing_;
    }

    ~HttpRequestImpl() override;

  protected:
    friend class HttpRequest;

    void setContentType(const std::string &contentType)
    {
        contentTypeString_ = contentType;
    }

    void setContentType(std::string &&contentType)
    {
        contentTypeString_ = std::move(contentType);
    }

    void parseContentTypeAndString() const
    {
        if (!flagForParsingContentType_)
        {
            flagForParsingContentType_ = true;
            auto &contentTypeString = getHeaderBy("content-type");
            if (contentTypeString == "")
            {
                contentType_ = CT_NONE;
            }
            else
            {
                auto pos = contentTypeString.find(';');
                if (pos != std::string::npos)
                {
                    contentType_ = parseContentType(
                        std::string_view(contentTypeString.data(), pos));
                }
                else
                {
                    contentType_ =
                        parseContentType(std::string_view(contentTypeString));
                }

                if (contentType_ == CT_NONE)
                    contentType_ = CT_CUSTOM;
                contentTypeString_ = contentTypeString;
            }
        }
    }

  private:
    void parseParameters() const;

    void parseParametersOnce() const
    {
        // Not multi-thread safe but good, because we basically call this
        // function in a single thread
        if (!flagForParsingParameters_)
        {
            flagForParsingParameters_ = true;
            parseParameters();
        }
    }

    void createTmpFile();
    void parseJson() const;
#ifdef USE_BROTLI
    StreamDecompressStatus decompressBodyBrotli() noexcept;
#endif
    StreamDecompressStatus decompressBodyGzip() noexcept;

    static constexpr const std::string_view emptySv_{""};

    mutable bool flagForParsingParameters_{false};
    mutable bool flagForParsingJson_{false};
    HttpMethod method_{Invalid};
    HttpMethod previousMethod_{Invalid};
    Version version_{Version::kUnknown};
    std::string path_;
    /// Contains the encoded `path_` if and only if `path_` is set in encoded
    /// form. If path is in a normal form and needed no decoding, then this will
    /// be empty, as we do not need to store a duplicate.
    std::string originalPath_;
    bool pathEncode_{true};
    std::string_view matchedPathPattern_{""};
    std::string query_;
    SafeStringMap<std::string> headers_;
    SafeStringMap<std::string> cookies_;
    std::optional<size_t> contentLengthHeaderValue_;
    size_t realContentLength_{0};
    mutable SafeStringMap<std::string> parameters_;
    mutable std::shared_ptr<Json::Value> jsonPtr_;
    SessionPtr sessionPtr_;
    mutable AttributesPtr attributesPtr_;
    trantor::InetAddress peer_;
    trantor::InetAddress local_;
    trantor::Date creationDate_;
    trantor::CertificatePtr peerCertificate_;
    std::unique_ptr<CacheFile> cacheFilePtr_;
    mutable std::unique_ptr<std::string> jsonParsingErrorPtr_;
    std::unique_ptr<std::string> expectPtr_;
    bool keepAlive_{true};
    bool isOnSecureConnection_{false};
    bool passThrough_{false};
    std::vector<std::string> routingParams_;

    ReqStreamStatus streamStatus_{ReqStreamStatus::None};
    std::function<void()> streamFinishCb_;
    RequestStreamReaderPtr streamReaderPtr_;
    std::exception_ptr streamExceptionPtr_;
    bool startProcessing_{false};
    std::weak_ptr<trantor::TcpConnection> connPtr_;

  protected:
    std::string content_;
    trantor::EventLoop *loop_;
    mutable ContentType contentType_{CT_TEXT_PLAIN};
    mutable bool flagForParsingContentType_{false};
    mutable std::string contentTypeString_;
};

using HttpRequestImplPtr = std::shared_ptr<HttpRequestImpl>;

inline void swap(HttpRequestImpl &one, HttpRequestImpl &two) noexcept
{
    one.swap(two);
}

}  // namespace drogon
