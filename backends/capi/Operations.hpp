#pragma once

#include "ErrorBoundary.hpp"
#include "Handles.hpp"

#include <span>

namespace iprop::capi {

// Owners must outlive the boundary handling exceptions from these operations.
// Every adapter uses this one decoder, validator, VM and result marshaller.
void loadProgram(iprop_program& program, std::span<const uint8_t> bytes,
                 ErrorContext& context);
CallOutcome legacyInputs(const iprop_generate_options& options, GenerationInputs& inputs);
// Consumes a completed C++ result into an unpublished, empty C handle. Names
// and pixels move; typed payloads are packed field by field, then released.
// The adapter owns the handle until every allocation succeeds.
void packResult(iprop_result& result, GenerationResult generation, const LoadedFile& file);
void generateProgram(const iprop_program& program, const GenerationInputs& inputs,
                     iprop_result& result, ErrorContext& context);
iprop_program_info_ex programInfo(const iprop_program& program) noexcept;
iprop_meta legacyMeta(const iprop_program& program) noexcept;
iprop_image resultImage(const iprop_result& result) noexcept;

} // namespace iprop::capi
