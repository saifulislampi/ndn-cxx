/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2013-2023 Regents of the University of California.
 *
 * This file is part of ndn-cxx library (NDN C++ library with eXperimental eXtensions).
 *
 * ndn-cxx library is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * ndn-cxx library is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received copies of the GNU General Public License and GNU Lesser
 * General Public License along with ndn-cxx, e.g., in COPYING.md file.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * See AUTHORS.md for complete list of ndn-cxx authors and contributors.
 */

#ifndef NDN_CXX_MGMT_NFD_CONTROLLER_HPP
#define NDN_CXX_MGMT_NFD_CONTROLLER_HPP

#include "ndn-cxx/mgmt/nfd/command-options.hpp"
#include "ndn-cxx/mgmt/nfd/control-command.hpp"
#include "ndn-cxx/mgmt/nfd/control-response.hpp"
#include "ndn-cxx/security/interest-signer.hpp"
#include "ndn-cxx/security/key-chain.hpp"
#include "ndn-cxx/security/validator-null.hpp"
#include "ndn-cxx/security/validator.hpp"
#include "ndn-cxx/util/segment-fetcher.hpp"

namespace ndn {

class Face;

namespace nfd {

/**
 * \defgroup management Management
 * \brief Classes and data structures to manage NDN forwarder.
 */

/**
 * \ingroup management
 * \brief NFD Management protocol client.
 * \sa https://redmine.named-data.net/projects/nfd/wiki/Management
 */
class Controller : noncopyable
{
public:
  /**
   * \brief Callback on command success.
   */
  using CommandSuccessCallback = std::function<void(const ControlParameters&)>;

  /**
   * \brief Callback on command failure.
   */
  using CommandFailureCallback = std::function<void(const ControlResponse&)>;
  using CommandFailCallback = CommandFailureCallback; // backward compat

  /**
   * \brief Callback on dataset retrieval success.
   */
  template<typename Dataset>
  using DatasetSuccessCallback = std::function<void(const std::invoke_result_t<decltype(&Dataset::parseResult),
                                                                               Dataset, ConstBufferPtr>&)>;

  /**
   * \brief Callback on dataset retrieval failure.
   */
  using DatasetFailureCallback = std::function<void(uint32_t code, const std::string& reason)>;
  using DatasetFailCallback = DatasetFailureCallback; // backward compat

  /**
   * \brief Construct a Controller that uses \p face as transport and \p keyChain to sign commands.
   */
  Controller(Face& face, KeyChain& keyChain,
             security::Validator& validator = security::getAcceptAllValidator());

  ~Controller();

  /**
   * \brief Start command execution.
   */
  template<typename Command>
  void
  start(const ControlParameters& parameters,
        const CommandSuccessCallback& onSuccess,
        const CommandFailureCallback& onFailure,
        const CommandOptions& options = CommandOptions())
  {
    startCommand(std::make_shared<Command>(), parameters, onSuccess, onFailure, options);
  }

  /**
   * \brief Start dataset fetching.
   */
  template<typename Dataset>
  std::enable_if_t<std::is_default_constructible_v<Dataset>>
  fetch(const DatasetSuccessCallback<Dataset>& onSuccess,
        const DatasetFailureCallback& onFailure,
        const CommandOptions& options = CommandOptions())
  {
    fetchDataset(std::make_shared<Dataset>(), onSuccess, onFailure, options);
  }

  /**
   * \brief Start dataset fetching.
   */
  template<typename Dataset, typename ParamType>
  void
  fetch(ParamType&& param,
        const DatasetSuccessCallback<Dataset>& onSuccess,
        const DatasetFailureCallback& onFailure,
        const CommandOptions& options = CommandOptions())
  {
    fetchDataset(std::make_shared<Dataset>(std::forward<ParamType>(param)),
                 onSuccess, onFailure, options);
  }

private:
  void
  startCommand(const shared_ptr<ControlCommand>& command,
               const ControlParameters& parameters,
               const CommandSuccessCallback& onSuccess,
               const CommandFailureCallback& onFailure,
               const CommandOptions& options);

  void
  processCommandResponse(const Data& data,
                         const shared_ptr<ControlCommand>& command,
                         const CommandSuccessCallback& onSuccess,
                         const CommandFailureCallback& onFailure);

  void
  processValidatedCommandResponse(const Data& data,
                                  const shared_ptr<ControlCommand>& command,
                                  const CommandSuccessCallback& onSuccess,
                                  const CommandFailureCallback& onFailure);

  template<typename Dataset>
  void
  fetchDataset(shared_ptr<Dataset> dataset,
               const DatasetSuccessCallback<Dataset>& onSuccess,
               const DatasetFailureCallback& onFailure,
               const CommandOptions& options);

  void
  fetchDataset(const Name& prefix,
               const std::function<void(ConstBufferPtr)>& processResponse,
               const DatasetFailureCallback& onFailure,
               const CommandOptions& options);

  void
  processDatasetFetchError(const DatasetFailureCallback& onFailure, uint32_t code, std::string msg);

public:
  /// Error code for timeout.
  static constexpr uint32_t ERROR_TIMEOUT = 10060;

  /// Error code for network %Nack.
  static constexpr uint32_t ERROR_NACK = 10800;

  /// Error code for response validation failure.
  static constexpr uint32_t ERROR_VALIDATION = 10021;

  /// Error code for server error.
  static constexpr uint32_t ERROR_SERVER = 500;

  /// Inclusive lower bound of error codes.
  static constexpr uint32_t ERROR_LBOUND = 400;

protected:
  Face& m_face;
  KeyChain& m_keyChain;
  security::Validator& m_validator;
  security::InterestSigner m_signer;

NDN_CXX_PUBLIC_WITH_TESTS_ELSE_PROTECTED:
  std::set<shared_ptr<util::SegmentFetcher>> m_fetchers;
};

template<typename Dataset>
void
Controller::fetchDataset(shared_ptr<Dataset> dataset,
                         const DatasetSuccessCallback<Dataset>& onSuccess,
                         const DatasetFailureCallback& onFailure,
                         const CommandOptions& options)
{
  Name prefix = dataset->getDatasetPrefix(options.getPrefix());
  fetchDataset(prefix,
    [dataset = std::move(dataset), onSuccess, onFailure] (ConstBufferPtr payload) {
      std::invoke_result_t<decltype(&Dataset::parseResult), Dataset, ConstBufferPtr> result;
      try {
        result = dataset->parseResult(std::move(payload));
      }
      catch (const tlv::Error& e) {
        if (onFailure)
          onFailure(ERROR_SERVER, e.what());
        return;
      }
      if (onSuccess)
        onSuccess(result);
    },
    onFailure, options);
}

} // namespace nfd
} // namespace ndn

#endif // NDN_CXX_MGMT_NFD_CONTROLLER_HPP
