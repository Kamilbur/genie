/**
 * Copyright 2018-2024 The Genie Authors.
 * @file
 * @copyright This file is part of Genie. See LICENSE and/or
 * https://github.com/MueFab/genie for more details.
 */

#include "genie/shared/api.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>  // NOLINT
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "genie/core/global_cfg.h"
#include "genie/core/flow_graph_decode.h"
#include "genie/core/meta/dataset.h"
#include "genie/core/meta/external_ref/fasta.h"
#include "genie/core/parameter/data_unit.h"
#include "genie/core/parameter/parameter_set.h"
#include "genie/entropy/bsc/param_decoder.h"
#include "genie/entropy/paramcabac/decoder.h"
#include "genie/entropy/lzma/param_decoder.h"
#include "genie/entropy/zstd/param_decoder.h"
#include "genie/format/fasta/manager.h"
#include "genie/format/fastq/exporter.h"
#include "genie/format/mgb/access_unit.h"
#include "genie/format/mgb/importer.h"
#include "genie/format/mgb/raw_reference.h"
#include "genie/entropy/bsc/decoder.h"
#include "genie/entropy/gabac/decoder.h"
#include "genie/entropy/lzma/decoder.h"
#include "genie/entropy/zstd/decoder.h"
#include "genie/name/tokenizer/decoder.h"
#include "genie/name/write_out/decoder.h"
#include "genie/quality/calq/decoder.h"
#include "genie/quality/paramqv1/qv_coding_config_1.h"
#include "genie/read/localassembly/decoder.h"
#include "genie/read/lowlatency/decoder.h"
#include "genie/read/refcoder/decoder.h"
#include "genie/read/spring/decoder.h"
#include "genie/util/log.h"

namespace {

std::mutex g_log_severity_mutex;
bool g_log_severity_configured = false;

std::string FileExtension(const std::string& path) {
  const auto pos = path.find_last_of('.');
  if (pos == std::string::npos) {
    return {};
  }
  std::string ext = path.substr(pos + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return ext == "gz" ? FileExtension(path.substr(0, pos)) : ext;
}

bool IsMgbFile(const char* path) {
  return path != nullptr && FileExtension(path) == "mgb";
}

void DetectSharedModules() {
  static std::once_flag once;
  std::call_once(once, [] {
    {
      std::lock_guard lock(g_log_severity_mutex);
      if (!g_log_severity_configured) {
        genie::util::Logger::GetInstance().SetSeverityLevel(
            genie::util::Logger::Severity::WARNING);
      }
    }

    auto& ind_park = genie::core::GlobalCfg::GetSingleton().GetIndustrialPark();
    ind_park.RegisterConstructor<
        genie::core::parameter::desc_pres::DecoderRegular>(
        genie::entropy::paramcabac::kModeCabac,
        &genie::entropy::paramcabac::DecoderRegular::create);
    ind_park.RegisterConstructor<
        genie::core::parameter::desc_pres::DecoderRegular>(
        genie::entropy::zstd::kModeZstd,
        &genie::entropy::zstd::DecoderRegular::create);
    ind_park.RegisterConstructor<
        genie::core::parameter::desc_pres::DecoderRegular>(
        genie::entropy::lzma::kModeLzma,
        &genie::entropy::lzma::DecoderRegular::create);
    ind_park.RegisterConstructor<
        genie::core::parameter::desc_pres::DecoderRegular>(
        genie::entropy::bsc::DecoderRegular::mode_bsc_,
        &genie::entropy::bsc::DecoderRegular::Create);
    ind_park.RegisterConstructor<
        genie::core::parameter::desc_pres::DecoderTokenType>(
        genie::entropy::paramcabac::kModeCabac,
        &genie::entropy::paramcabac::DecoderTokenType::create);
    ind_park.RegisterConstructor<genie::core::parameter::QualityValues>(
        genie::quality::paramqv1::kModeQv1,
        &genie::quality::paramqv1::QualityValues1::create);
  });
}

uint8_t AddFastaReference(
    const std::string& fasta_file_path, genie::core::FlowGraphDecode* flow,
    std::vector<std::unique_ptr<std::istream>>& input_files) {
  auto fasta_file = std::make_unique<std::ifstream>(fasta_file_path);
  if (!*fasta_file) {
    return GENIE_SHARED_INVALID_PARAMETER;
  }

  std::string fai =
      fasta_file_path.substr(0, fasta_file_path.find_last_of('.') + 1) + "fai";
  std::string sha =
      fasta_file_path.substr(0, fasta_file_path.find_last_of('.') + 1) +
      "sha256";

  if (!std::filesystem::exists(fai)) {
    std::ofstream fai_file(fai);
    genie::format::fasta::FastaReader::index(*fasta_file, fai_file);
  }
  if (!std::filesystem::exists(sha)) {
    std::ofstream sha_file(sha);
    std::ifstream fai_file(fai);
    if (!fai_file) {
      return GENIE_SHARED_INVALID_PARAMETER;
    }
    genie::format::fasta::FaiFile fai_reader(fai_file);
    genie::format::fasta::FastaReader::hash(fai_reader, *fasta_file, sha_file);
  }

  auto fai_file = std::make_unique<std::ifstream>(fai);
  auto sha_file = std::make_unique<std::ifstream>(sha);
  if (!*fai_file || !*sha_file) {
    return GENIE_SHARED_INVALID_PARAMETER;
  }

  input_files.push_back(std::move(fasta_file));
  input_files.push_back(std::move(fai_file));
  input_files.push_back(std::move(sha_file));
  flow->AddReferenceSource(std::make_unique<genie::format::fasta::Manager>(
      **(input_files.rbegin() + 2), **(input_files.rbegin() + 1),
      **input_files.rbegin(), &flow->GetRefMgr(), fasta_file_path));
  return GENIE_SHARED_SUCCESS;
}

uint8_t AddReference(
    const std::string& reference_file, genie::core::FlowGraphDecode* flow,
    std::vector<std::unique_ptr<std::istream>>& input_files) {
  if (reference_file.empty()) {
    return GENIE_SHARED_SUCCESS;
  }

  const std::string ext = FileExtension(reference_file);
  if (ext == "fasta" || ext == "fa") {
    return AddFastaReference(reference_file, flow, input_files);
  }
  if (ext == "mgb") {
    auto ref_stream =
        std::make_unique<std::ifstream>(reference_file, std::ios::binary);
    if (!*ref_stream) {
      return GENIE_SHARED_INVALID_PARAMETER;
    }
    input_files.emplace_back(std::move(ref_stream));
    flow->AddImporter(std::make_unique<genie::format::mgb::Importer>(
        *input_files.back(), &flow->GetRefMgr(), flow->GetRefDecoder(), true));
    return GENIE_SHARED_SUCCESS;
  }
  return GENIE_SHARED_INVALID_PARAMETER;
}

std::string ReferenceFromSidecarJson(const std::string& input_file) {
  const std::string json_path = input_file + ".json";
  if (!std::filesystem::exists(json_path) ||
      std::filesystem::file_size(json_path) == 0) {
    return {};
  }
  genie::core::meta::Dataset data(
      nlohmann::json::parse(std::ifstream(json_path)));
  if (!data.GetReference()) {
    return {};
  }
  const auto& fasta =
      dynamic_cast<const genie::core::meta::external_ref::Fasta&>(
          data.GetReference()->GetBase());
  const std::string& uri = fasta.GetUri();
  const std::string scheme = "file://";
  if (uri.substr(0, scheme.length()) != scheme) {
    return {};
  }
  std::string path = uri.substr(scheme.length());
  return std::filesystem::exists(path) ? path : std::string();
}

uint8_t ValidateAccessUnitId(const std::string& input_file,
                             const uint64_t access_unit_id) {
  if (access_unit_id > UINT32_MAX) {
    return GENIE_SHARED_ACCESS_UNIT_NOT_FOUND;
  }
  std::ifstream stream(input_file, std::ios::binary);
  if (!stream) {
    return GENIE_SHARED_INVALID_PARAMETER;
  }
  genie::util::BitReader reader(stream);
  std::map<size_t, genie::core::parameter::EncodingSet> parameter_sets;

  while (true) {
    const auto type =
        reader.Read<genie::core::parameter::DataUnit::DataUnitType>();
    if (!reader.IsStreamGood()) {
      reader.ClearStreamState();
      break;
    }
    switch (type) {
      case genie::core::parameter::DataUnit::DataUnitType::kParameterSet: {
        genie::core::parameter::ParameterSet set(reader);
        parameter_sets.emplace(set.GetId(), set.GetEncodingSet());
        break;
      }
      case genie::core::parameter::DataUnit::DataUnitType::kRawReference:
        genie::format::mgb::RawReference(reader, true, true);
        break;
      case genie::core::parameter::DataUnit::DataUnitType::kAccessUnit: {
        genie::format::mgb::AccessUnit au(parameter_sets, reader, true);
        if (au.GetHeader().GetId() == static_cast<uint32_t>(access_unit_id)) {
          return GENIE_SHARED_SUCCESS;
        }
        reader.SkipAlignedBytes(au.GetPayloadSize());
        break;
      }
      default:
        return GENIE_SHARED_INVALID_BITSTREAM;
    }
  }
  return GENIE_SHARED_ACCESS_UNIT_NOT_FOUND;
}

uint8_t CountAccessUnitsInMgb(const std::string& input_file,
                              uint64_t* output_count) {
  std::ifstream stream(input_file, std::ios::binary);
  if (!stream) {
    return GENIE_SHARED_INVALID_PARAMETER;
  }

  genie::util::BitReader reader(stream);
  std::map<size_t, genie::core::parameter::EncodingSet> parameter_sets;
  uint64_t count = 0;

  while (true) {
    const auto type =
        reader.Read<genie::core::parameter::DataUnit::DataUnitType>();
    if (!reader.IsStreamGood()) {
      reader.ClearStreamState();
      *output_count = count;
      return GENIE_SHARED_SUCCESS;
    }

    switch (type) {
      case genie::core::parameter::DataUnit::DataUnitType::kParameterSet: {
        genie::core::parameter::ParameterSet set(reader);
        parameter_sets.emplace(set.GetId(), set.GetEncodingSet());
        break;
      }
      case genie::core::parameter::DataUnit::DataUnitType::kRawReference:
        genie::format::mgb::RawReference(reader, true, true);
        break;
      case genie::core::parameter::DataUnit::DataUnitType::kAccessUnit: {
        genie::format::mgb::AccessUnit au(parameter_sets, reader, true);
        ++count;
        reader.SkipAlignedBytes(au.GetPayloadSize());
        break;
      }
      default:
        return GENIE_SHARED_INVALID_BITSTREAM;
    }
  }
}

std::unique_ptr<genie::core::FlowGraphDecode> BuildSharedDecoder(
    const size_t threads, const std::string& working_dir) {
  auto flow = std::make_unique<genie::core::FlowGraphDecode>(threads);

  flow->AddReadCoder(std::make_unique<genie::read::refcoder::Decoder>());
  flow->AddReadCoder(std::make_unique<genie::read::localassembly::Decoder>());
  auto low_latency = std::make_unique<genie::read::lowlatency::Decoder>();
  flow->SetRefDecoder(low_latency.get());
  flow->AddReadCoder(std::move(low_latency));
  flow->AddReadCoder(std::make_unique<genie::read::spring::Decoder>(
      working_dir, true, false));
  flow->AddReadCoder(std::make_unique<genie::read::spring::Decoder>(
      working_dir, true, true));
  flow->SetReadCoderSelector([](const genie::core::AccessUnit& au) -> size_t {
    if (au.GetParameters().IsComputedReference()) {
      switch (au.GetParameters().GetComputedRef().GetAlgorithm()) {
        case genie::core::parameter::ComputedRef::Algorithm::kGlobalAssembly:
          return au.GetParameters().GetNumberTemplateSegments() >= 2 ? 4 : 3;
        case genie::core::parameter::ComputedRef::Algorithm::kLocalAssembly:
          return 1;
        case genie::core::parameter::ComputedRef::Algorithm::kRefTransform:
        case genie::core::parameter::ComputedRef::Algorithm::kPushIn:
        default:
          UTILS_DIE("Unsupported computed reference decoding mode");
      }
    }
    return au.GetClassType() == genie::core::record::ClassType::kClassU ? 2 : 0;
  });

  flow->AddQvCoder(std::make_unique<genie::quality::calq::Decoder>());
  flow->SetQvSelector([](const genie::core::parameter::QualityValues& param,
                         const std::vector<std::string>&,
                         const std::vector<uint64_t>&,
                         genie::core::AccessUnit::Descriptor&) -> size_t {
    UTILS_DIE_IF(param.GetMode() != 1, "Unsupported QV decoding mode");
    return 0;
  });

  flow->AddNameCoder(std::make_unique<genie::name::tokenizer::Decoder>());
  flow->AddNameCoder(std::make_unique<genie::name::write_out::Decoder>());
  flow->SetNameSelector(
      [](const genie::core::AccessUnit::Descriptor& d) -> size_t {
        return d.GetSize() <= 1 ? 1 : 0;
      });

  flow->AddEntropyCoder(std::make_unique<genie::entropy::gabac::Decoder>());
  flow->AddEntropyCoder(std::make_unique<genie::entropy::lzma::Decoder>());
  flow->AddEntropyCoder(std::make_unique<genie::entropy::zstd::Decoder>());
  flow->AddEntropyCoder(std::make_unique<genie::entropy::bsc::Decoder>());
  flow->SetEntropyCoderSelector(
      [](const genie::core::parameter::DescriptorSubSequenceCfg& cfg,
         genie::core::AccessUnit::Descriptor&, bool) -> size_t {
        UTILS_DIE_IF(cfg.IsClassSpecific(),
                     "Class-specific entropy decoding not supported");
        auto* desc = dynamic_cast<
            const genie::core::parameter::desc_pres::DescriptorPresent*>(
            &cfg.Get());
        UTILS_DIE_IF(desc == nullptr, "Decoder configuration not present");
        if (dynamic_cast<
                const genie::core::parameter::desc_pres::DecoderTokenType*>(
                &desc->GetDecoder()) != nullptr) {
          return 0;
        }
        auto* decoder = dynamic_cast<
            const genie::core::parameter::desc_pres::DecoderRegular*>(
            &desc->GetDecoder());
        UTILS_DIE_IF(decoder == nullptr, "Unknown decoder configuration");
        UTILS_DIE_IF(
            decoder->GetMode() > 3,
            "Unknown entropy decoder: " + std::to_string(decoder->GetMode()));
        return decoder->GetMode();
      });

  flow->SetExporterSelector(
      [](const genie::core::record::Chunk&) -> size_t { return 0; });
  return flow;
}

uint8_t DecompressAccessUnitToStream(const std::string& input_path,
                                     const uint64_t access_unit_id,
                                     std::ostream& output_stream,
                                     const char* reference_file,
                                     const char* working_dir,
                                     const uint64_t threads) {
  const uint8_t id_status = ValidateAccessUnitId(input_path, access_unit_id);
  if (id_status != GENIE_SHARED_SUCCESS) {
    return id_status;
  }

  std::string reference_path = reference_file == nullptr ? "" : reference_file;
  if (reference_path.empty()) {
    reference_path = ReferenceFromSidecarJson(input_path);
  }

  auto flow = BuildSharedDecoder(
      threads == 0 ? 1 : static_cast<size_t>(threads),
      working_dir == nullptr ? "." : working_dir);
  flow->AddRandomAccessUnits(
      {static_cast<int>(static_cast<uint32_t>(access_unit_id))});

  std::vector<std::unique_ptr<std::istream>> input_files;

  uint8_t status = AddReference(reference_path, flow.get(), input_files);
  if (status != GENIE_SHARED_SUCCESS) {
    return status;
  }

  auto input_stream =
      std::make_unique<std::ifstream>(input_path, std::ios::binary);
  if (!*input_stream) {
    return GENIE_SHARED_INVALID_PARAMETER;
  }
  input_files.emplace_back(std::move(input_stream));
  flow->AddImporter(std::make_unique<genie::format::mgb::Importer>(
      *input_files.back(), &flow->GetRefMgr(), flow->GetRefDecoder(), false));
  flow->AddExporter(
      std::make_unique<genie::format::fastq::Exporter>(output_stream));

  flow->Run();
  return GENIE_SHARED_SUCCESS;
}

}  // namespace

extern "C" {

uint8_t GenieSetLogSeverity(const uint8_t severity) {
  if (severity > 3) {
    return GENIE_SHARED_INVALID_PARAMETER;
  }
  {
    std::lock_guard lock(g_log_severity_mutex);
    genie::util::Logger::GetInstance().SetSeverityLevel(
        static_cast<genie::util::Logger::Severity>(severity));
    g_log_severity_configured = true;
  }
  return GENIE_SHARED_SUCCESS;
}

const char* GenieSharedStrerror(const uint8_t code) {
  switch (code) {
    case GENIE_SHARED_SUCCESS:
      return "Success";
    case GENIE_SHARED_INVALID_PARAMETER:
      return "Invalid parameter";
    case GENIE_SHARED_INVALID_BITSTREAM:
      return "Invalid bitstream";
    case GENIE_SHARED_ACCESS_UNIT_NOT_FOUND:
      return "Access unit not found";
    case GENIE_SHARED_UNLISTED_ERROR:
      return "Unlisted error";
    default:
      return "Unknown Genie shared-library error";
  }
}

uint8_t GenieGetAccessUnitCount(const char* input_file,
                                uint64_t* output_count) {
  if (!IsMgbFile(input_file) || output_count == nullptr) {
    return GENIE_SHARED_INVALID_PARAMETER;
  }

  try {
    DetectSharedModules();
    return CountAccessUnitsInMgb(input_file, output_count);
  } catch (const std::exception&) {
    return GENIE_SHARED_INVALID_BITSTREAM;
  } catch (...) {
    return GENIE_SHARED_UNLISTED_ERROR;
  }
}

uint8_t GenieDecompressAccessUnit(const char* input_file,
                                  const uint64_t access_unit_id,
                                  const char* output_file,
                                  const char* reference_file,
                                  const char* working_dir,
                                  const uint64_t threads) {
  if (!IsMgbFile(input_file) || output_file == nullptr ||
      FileExtension(output_file) != "fastq") {
    return GENIE_SHARED_INVALID_PARAMETER;
  }

  try {
    DetectSharedModules();
    std::ofstream output_stream(output_file);
    if (!output_stream) {
      return GENIE_SHARED_INVALID_PARAMETER;
    }
    return DecompressAccessUnitToStream(input_file, access_unit_id,
                                        output_stream, reference_file,
                                        working_dir, threads);
  } catch (const std::exception&) {
    return GENIE_SHARED_INVALID_BITSTREAM;
  } catch (...) {
    return GENIE_SHARED_UNLISTED_ERROR;
  }
}

uint8_t GenieDecompressAccessUnitToFastq(const char* input_file,
                                         const uint64_t access_unit_id,
                                         const char* reference_file,
                                         const char* working_dir,
                                         const uint64_t threads,
                                         char** output_data,
                                         uint64_t* output_size) {
  if (!IsMgbFile(input_file) || output_data == nullptr ||
      output_size == nullptr) {
    return GENIE_SHARED_INVALID_PARAMETER;
  }
  *output_data = nullptr;
  *output_size = 0;

  try {
    DetectSharedModules();
    std::stringstream output_stream;
    const uint8_t status = DecompressAccessUnitToStream(
        input_file, access_unit_id, output_stream, reference_file, working_dir,
        threads);
    if (status != GENIE_SHARED_SUCCESS) {
      return status;
    }

    const std::string fastq = output_stream.str();
    auto* data = static_cast<char*>(std::malloc(fastq.size() + 1));
    if (data == nullptr) {
      return GENIE_SHARED_UNLISTED_ERROR;
    }
    std::memcpy(data, fastq.data(), fastq.size());
    data[fastq.size()] = '\0';

    *output_data = data;
    *output_size = fastq.size();
    return GENIE_SHARED_SUCCESS;
  } catch (const std::exception&) {
    return GENIE_SHARED_INVALID_BITSTREAM;
  } catch (...) {
    return GENIE_SHARED_UNLISTED_ERROR;
  }
}

void GenieFree(void* ptr) { std::free(ptr); }

}  // extern "C"
