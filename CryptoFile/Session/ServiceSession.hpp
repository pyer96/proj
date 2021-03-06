#ifndef __CryptoFile__Session__Service__Session__
#define __CryptoFile__Session__Service__Session__

#include "CryptoFile/Db/CloudService.hpp"
#include "CryptoFile/Session/Auth/ServiceAuthenticator.hpp"

#include <cpprest/http_client.h>

namespace cryptofile {
namespace session {
using namespace web::http;
class ServiceSession {
private:
  auth::ServiceAuthenticator m_service_authenticator;
  client::http_client_config m_http_client_config;
  const db::CloudService m_cloud_service_id;

public:
  ServiceSession(db::CloudService cloud_service_id, std::string auth_endpoint,
                 std::string access_endpoint, std::string client_id,
                 std::string client_secrets, std::string redirect_uri,
                 std::string scope = std::string());
  virtual ~ServiceSession() {}
  const auto &get_cloud_service_id() { return m_cloud_service_id; }
  const auto &http_client_config() const { return m_http_client_config; }
  virtual web::json::value upload_file(std::vector<std::uint8_t> file_data,
                                       std::string file_name) = 0;
  virtual std::vector<std::uint8_t>
  download_file(std::string cloud_file_id) = 0;
  web::json::value upload_file(std::string file_path);
};
} // namespace session
} // namespace cryptofile
#endif
