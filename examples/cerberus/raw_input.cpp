// Copyright (C) 2010-2015 Joshua Boyce
// See the file COPYING for copying permission.

#include "raw_input.hpp"

#include <cstdint>

#include <windows.h>

#include <hadesmem/config.hpp>
#include <hadesmem/detail/last_error_preserver.hpp>
#include <hadesmem/detail/trace.hpp>
#include <hadesmem/patcher.hpp>

#include "callbacks.hpp"
#include "helpers.hpp"
#include "main.hpp"

namespace
{
hadesmem::cerberus::Callbacks<hadesmem::cerberus::OnGetRawInputBufferCallback>&
  GetOnGetRawInputBufferCallbacks()
{
  static hadesmem::cerberus::Callbacks<
    hadesmem::cerberus::OnGetRawInputBufferCallback> callbacks;
  return callbacks;
}

hadesmem::cerberus::Callbacks<hadesmem::cerberus::OnGetRawInputDataCallback>&
  GetOnGetRawInputDataCallbacks()
{
  static hadesmem::cerberus::Callbacks<
    hadesmem::cerberus::OnGetRawInputDataCallback> callbacks;
  return callbacks;
}

class RawInputImpl : public hadesmem::cerberus::RawInputInterface
{
public:
  virtual std::size_t RegisterOnGetRawInputBuffer(std::function<
    hadesmem::cerberus::OnGetRawInputBufferCallback> const& callback) final
  {
    auto& callbacks = GetOnGetRawInputBufferCallbacks();
    return callbacks.Register(callback);
  }

  virtual void UnregisterOnGetRawInputBuffer(std::size_t id) final
  {
    auto& callbacks = GetOnGetRawInputBufferCallbacks();
    return callbacks.Unregister(id);
  }

  virtual std::size_t RegisterOnGetRawInputData(std::function<
    hadesmem::cerberus::OnGetRawInputDataCallback> const& callback) final
  {
    auto& callbacks = GetOnGetRawInputDataCallbacks();
    return callbacks.Register(callback);
  }

  virtual void UnregisterOnGetRawInputData(std::size_t id) final
  {
    auto& callbacks = GetOnGetRawInputDataCallbacks();
    return callbacks.Unregister(id);
  }
};

std::unique_ptr<hadesmem::PatchDetour<decltype(&::GetRawInputBuffer)>>&
  GetGetRawInputBufferDetour() HADESMEM_DETAIL_NOEXCEPT
{
  static std::unique_ptr<hadesmem::PatchDetour<decltype(&::GetRawInputBuffer)>>
    detour;
  return detour;
}

std::unique_ptr<hadesmem::PatchDetour<decltype(&::GetRawInputData)>>&
  GetGetRawInputDataDetour() HADESMEM_DETAIL_NOEXCEPT
{
  static std::unique_ptr<hadesmem::PatchDetour<decltype(&::GetRawInputData)>>
    detour;
  return detour;
}

std::pair<void*, SIZE_T>& GetUser32Module() HADESMEM_DETAIL_NOEXCEPT
{
  static std::pair<void*, SIZE_T> module{nullptr, 0};
  return module;
}

extern "C" UINT WINAPI
  GetRawInputBufferDetour(hadesmem::PatchDetourBase* detour,
                          PRAWINPUT data,
                          PUINT size,
                          UINT size_header) HADESMEM_DETAIL_NOEXCEPT
{
  hadesmem::detail::LastErrorPreserver last_error_preserver;

  HADESMEM_DETAIL_TRACE_NOISY_FORMAT_A(
    "Args: [%p] [%p] [%u].", data, size, size_header);

  auto const get_raw_input_buffer =
    detour->GetTrampolineT<decltype(&::GetRawInputBuffer)>();
  last_error_preserver.Revert();
  auto const ret = get_raw_input_buffer(data, size, size_header);
  last_error_preserver.Update();
  HADESMEM_DETAIL_TRACE_NOISY_FORMAT_A("Ret: [%u].", ret);

  auto const& callbacks = GetOnGetRawInputBufferCallbacks();
  bool handled = false;
  UINT retval{ret};
  callbacks.Run(data, size, size_header, &handled, &retval);

  return retval;
}

extern "C" UINT WINAPI
  GetRawInputDataDetour(hadesmem::PatchDetourBase* detour,
                        HRAWINPUT raw_input,
                        UINT command,
                        LPVOID data,
                        PUINT size,
                        UINT size_header) HADESMEM_DETAIL_NOEXCEPT
{
  hadesmem::detail::LastErrorPreserver last_error_preserver;

  HADESMEM_DETAIL_TRACE_NOISY_FORMAT_A("Args: [%p] [%u] [%p] [%p] [%u].",
                                       raw_input,
                                       command,
                                       data,
                                       size,
                                       size_header);

  auto const get_raw_input_data =
    detour->GetTrampolineT<decltype(&::GetRawInputData)>();
  last_error_preserver.Revert();
  auto const ret =
    get_raw_input_data(raw_input, command, data, size, size_header);
  last_error_preserver.Update();
  HADESMEM_DETAIL_TRACE_NOISY_FORMAT_A("Ret: [%u].", ret);

  auto const& callbacks = GetOnGetRawInputDataCallbacks();
  bool handled = false;
  UINT retval{ret};
  callbacks.Run(raw_input, command, data, size, size_header, &handled, &retval);

  return retval;
}
}

namespace hadesmem
{
namespace cerberus
{
RawInputInterface& GetRawInputInterface() HADESMEM_DETAIL_NOEXCEPT
{
  static RawInputImpl raw_input_impl;
  return raw_input_impl;
}

void InitializeRawInput()
{
  auto& helper = GetHelperInterface();
  helper.InitializeSupportForModule(L"USER32",
                                    DetourUser32ForRawInput,
                                    UndetourUser32ForRawInput,
                                    GetUser32Module);
}

void DetourUser32ForRawInput(HMODULE base)
{
  auto const& process = GetThisProcess();
  auto& module = GetUser32Module();
  auto& helper = GetHelperInterface();
  if (helper.CommonDetourModule(process, L"user32", base, module))
  {
    DetourFunc(process,
               base,
               "GetRawInputBuffer",
               GetGetRawInputBufferDetour(),
               GetRawInputBufferDetour);
    DetourFunc(process,
               base,
               "GetRawInputData",
               GetGetRawInputDataDetour(),
               GetRawInputDataDetour);
  }
}

void UndetourUser32ForRawInput(bool remove)
{
  auto& module = GetUser32Module();
  auto& helper = GetHelperInterface();
  if (helper.CommonUndetourModule(L"user32", module))
  {
    UndetourFunc(L"GetRawInputBuffer", GetGetRawInputBufferDetour(), remove);
    UndetourFunc(L"GetRawInputData", GetGetRawInputDataDetour(), remove);

    module = std::make_pair(nullptr, 0);
  }
}
}
}
