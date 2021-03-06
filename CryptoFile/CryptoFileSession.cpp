#include "CryptoFile/CryptoFileSession.hpp"

#include "CryptoFile/Aont/Aont.hpp"
#include "CryptoFile/Db/DatabaseValues.hpp"
#include "CryptoFile/Session/DriveSession.hpp"
#include "CryptoFile/Session/DropboxSession.hpp"
#include "Tools/Checksum.hpp"
#include "Tools/Files.hpp"
#include "Tools/Hex.hpp"

#include <algorithm>
#include <boost/filesystem.hpp>
#include <fmt/format.h>
#include <iostream>
#include <numeric>

namespace cryptofile {
CryptoFileSession::CryptoFileSession(
    std::vector<db::CloudService> clouds_to_authenticate) {
  std::cerr << "cloud_to_authenticate.size() = "
            << clouds_to_authenticate.size() << '\n';
  for (const auto &cloud_to_authenticate : clouds_to_authenticate) {
    switch (cloud_to_authenticate) {
    case db::CloudService::Drive:
      std::cerr << "emplace_back(DriveSession)\n";
      m_authenticated_clouds.emplace_back(
          std::make_shared<session::DriveSession>());
      break;
    case db::CloudService::Dropbox:
      std::cerr << "emplace_back(DropboxSession)\n";
      m_authenticated_clouds.emplace_back(
          std::make_shared<session::DropboxSession>());
      break;
    }
  }
  std::cerr << "db::get_original_files(m_original_files)\n";
  db::get_original_files(m_original_files);
}

void CryptoFileSession::list_original_files() {
  std::cout << "File n.\tName\n";
  for (std::size_t i = 0; i < m_original_files.size(); ++i) {
    std::cout << i << '\t' << m_original_files[i]->name() << '\n';
  }
}

void CryptoFileSession::save_file_on_clouds(
    std::size_t section_number, boost::filesystem::path file_path,
    std::vector<db::CloudService> selected_clouds) {

  std::vector<std::uint8_t> data;
  if (tools::read_from_file(data, file_path.c_str()) || section_number < 1) {
    return;
  }

  std::string original_file_name = file_path.filename().c_str();
  // DEBUG
  std::cerr << "original_file_name = " << original_file_name << '\n';

  std::string checksum;
  tools::calculate_sha256(data, checksum);
  // DEBUG
  std::cerr << "original_file_checksum = " << checksum << '\n';

  // We make a vector of pointers to the selected clouds for upload
  std::vector<std::shared_ptr<session::ServiceSession>>
      selected_authenticated_clouds;
  for (auto &cloud : selected_clouds) {
    const auto iterator = std::find_if(
        m_authenticated_clouds.begin(), m_authenticated_clouds.end(),
        [cloud](const std::shared_ptr<session::ServiceSession> &session) {
          return session->get_cloud_service_id() == cloud;
        });
    if (iterator != m_authenticated_clouds.end()) {
      selected_authenticated_clouds.emplace_back(*iterator);
    }
  }

  // DEBUG
  std::cerr << "selected_cloud_id:\n";
  for (const auto &selected_cloud : selected_authenticated_clouds) {
    std::cerr << '\t'
              << static_cast<int>(selected_cloud->get_cloud_service_id())
              << '\n';
  }

  aont::aont_mask(
      data, section_number,
      [original_files = &m_original_files, &original_file_name,
       &selected_authenticated_clouds, checksum](
          std::vector<std::unique_ptr<cryptofile::aont::Section>> &sections) {
        // make new db::OriginalFile
        auto &original_file = original_files->emplace_back(
            std::make_unique<cryptofile::db::OriginalFile>());
        original_file->set_name(original_file_name);
        original_file->set_checksum(checksum);
        auto &original_file_sections = original_file->sections();

        // vector<Db::Section>.size() == vector<aont::Section>.size()
        original_file_sections.reserve(sections.size());

        // Randomization of the sections order
        std::vector<std::size_t> index_buffer(sections.size());
        std::iota(index_buffer.begin(), index_buffer.end(), 0);
        std::random_shuffle(index_buffer.begin(), index_buffer.end());

        for (std::size_t i = 0; i < sections.size(); ++i) {
          original_file_sections.emplace_back(std::make_unique<db::Section>());
        }

        for (std::size_t i = 0; i < sections.size();) {
          for (std::size_t j = 0;
               j < selected_authenticated_clouds.size() && i < sections.size();
               ++j) {
            std::cerr << "\n\n i = " << i << " \n j = " << j << "\n\n";
            std::string section_name;
            tools::random_hex_string_generator(12, section_name);

            auto response = selected_authenticated_clouds[j]->upload_file(
                sections[index_buffer[i]]->get_data(), section_name);
            auto &new_section = original_file_sections[index_buffer[i]];
            new_section->set_name(section_name);
            new_section->set_order(index_buffer[i]);
            new_section->set_cloud_service_id(static_cast<int>(
                selected_authenticated_clouds[j]->get_cloud_service_id()));
            new_section->set_section_cloud_id(response["id"].as_string());
            ++i;
            std::cout << "\nSection " << i << " uploaded on "
                   << (j == 0 && new_section->get_cloud_service_id()==1 ? "Drive" : "Dropbox");
          }
        }
        std::cerr << "NEW db::Section VECTOR\n";
        for (const auto &sec : original_file_sections) {
          std::cerr << sec->order() << '\n';
        }
        original_file->save();
      });
}

void CryptoFileSession::restore_file_from_clouds(
    std::size_t original_file_index,
    boost::filesystem::path restored_file_path) {
  if (original_file_index >= m_original_files.size())
    return;
  auto &file_to_restore = m_original_files[original_file_index];
  auto &sections_to_restore = file_to_restore->sections();
  std::vector<std::vector<std::uint8_t>> sections;
  sections.reserve(sections_to_restore.size());
  std::cerr << "SIZE = " << sections.size() << '\n';
  for (std::size_t i = 0; i < sections_to_restore.size(); ++i) {
    auto &section = sections_to_restore[i];
    std::cerr << i << " < " << sections_to_restore.size() << '\n';
    std::cerr << "order = " << section->order() << '\n';

    const auto cloud_service = std::find_if(
        m_authenticated_clouds.begin(), m_authenticated_clouds.end(),
        [cloud_id = section->cloud_service_id()](
            const std::shared_ptr<session::ServiceSession> &session) {
          return session->get_cloud_service_id() ==
                 static_cast<db::CloudService>(cloud_id);
        });

    if (cloud_service == m_authenticated_clouds.end()) {
      std::cerr << "The section is stored in a cloud service that has not been "
                   "authenticated yet\n";
      return;
    }
    std::cerr << "\ncloud id of the section to download = "
              << section->section_cloud_id() << '\n';
    auto pos = sections.emplace(
        sections.begin() + section->order(),
        (*cloud_service)->download_file(section->section_cloud_id()));
    std::cerr << "Section name====" << section->name() << '\n';
    std::cout << "\nSection " << i + 1 << "/" << sections_to_restore.size()
              << " downloaded\t\tSize -> " << pos->size()<<" (bytes)";
  }

  for (const auto &sec : sections) {
    std::cerr << "sec.size() = " << sec.size() << '\n';
  }

  aont::aont_restore(sections, [&restored_file_path,
                                &file_name = file_to_restore->name(),
                                &file_checksum = file_to_restore->checksum()](
                                   std::vector<std::uint8_t> &plain_data) {
    boost::filesystem::create_directories(restored_file_path.c_str());
    restored_file_path /= file_name;
    tools::write_2_file(plain_data, restored_file_path.c_str());
    std::string checksum;
    tools::calculate_sha256(plain_data, checksum);
    if (not checksum.compare(file_checksum)) {
      std::cout << fmt::format(
          "\n\"{}\" successfully restored! (sha256 verified: {})\n", file_name,
          checksum);
    }
  });
}
} // namespace cryptofile
