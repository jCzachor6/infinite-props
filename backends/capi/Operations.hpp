#pragma once

#include "ErrorBoundary.hpp"
#include "Handles.hpp"
#include "Options.hpp"

#include <span>

namespace iprop::capi {

// Owners must outlive the boundary handling exceptions from these operations.
// Every adapter uses this one decoder, validator, VM and result marshaller.
void loadProgram(iprop_program& program, std::span<const uint8_t> bytes,
                 ErrorContext& context);
CallOutcome legacyInputs(const iprop_generate_options& options, GenerationInputs& inputs);
// The one strict request check for new generation entries (generate_ex now,
// a future generation context later), before VM setup:
// options descriptor, then parameter-set identity (NULL means defaults), then
// variant, archetype and target canvas through the VM's shared range check.
// Every failure is an argument outcome with a stable reason; nothing is
// allocated and `out` is unchanged. The legacy entry keeps its setup traps.
CallOutcome checkGenerationRequest(const iprop_program& program,
                                   const iprop_generate_options_ex* options,
                                   const iprop_parameters* parameters,
                                   GenerateOptions& out) noexcept;
// Consumes a completed C++ result into an unpublished, empty C handle. Names
// and pixels move; typed payloads are packed field by field, then released.
// The adapter owns the handle until every allocation succeeds.
void packResult(iprop_result& result, GenerationResult generation, const LoadedFile& file);
// Runs the shared VM. With a parameter set, its values are passed by root-input
// ID and `inputs.uniforms` must be empty: names were resolved once, when the
// values were set. Without one, the VM resolves `inputs.uniforms` by name.
void generateProgram(const iprop_program& program, const GenerationInputs& inputs,
                     iprop_result& result, ErrorContext& context,
                     const iprop_parameters* parameters = nullptr);
iprop_program_info_ex programInfo(const iprop_program& program) noexcept;
iprop_meta legacyMeta(const iprop_program& program) noexcept;
iprop_image resultImage(const iprop_result& result) noexcept;

} // namespace iprop::capi
