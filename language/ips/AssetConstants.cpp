#include "ips/AssetConstants.hpp"
#include "ips/Fold.hpp"
#include "ips/Typing.hpp"
#include "ips/Suggestions.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/Samplers.hpp"
#include "iprop/ListOperations.hpp"
#include "iprop/math/Distribution.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace iprop::ips {

const Asset::Constant* findAssetConstant(const Program& program, const std::string& name) {
    for (const auto& constant : program.asset.constants)
        if (constant.name == name) return &constant;
    return nullptr;
}

bool constantBuiltin(const BuiltinSpec& spec, const BuiltinOverload& overload) {
    if (!isStorable(overload.result)) return false;
    if (spec.lowering == BuiltinLowering::ListLength) return true;
    if (overload.opcode) return detail::isConstantOperation(*findOpcode(*overload.opcode));
    return spec.lowering == BuiltinLowering::Opcode || spec.lowering == BuiltinLowering::Round ||
        spec.lowering == BuiltinLowering::ColorConstructor || spec.lowering == BuiltinLowering::Beta;
}

namespace {
std::string callName(const Expr& expression) {
    if (const auto* id = std::get_if<Identifier>(&expression)) return id->name;
    if (const auto* member = std::get_if<Member>(&expression)) {
        const auto base = callName(*member->object);
        if (!base.empty()) return base + "." + member->name;
    }
    return {};
}
f32 number(const Value& value) {
    if (const auto* i = std::get_if<i32>(&value)) return static_cast<f32>(*i);
    return std::get<f32>(value);
}

class Resolver {
public:
    explicit Resolver(const Program& program) : program_(program), typer_(parts_) {}

    // A probe of runtime expressions over an already resolved graph.
    Resolver(const Program& program, AssetDeclarations declarations) : Resolver(program) {
        registerDeclarations();
        values_ = std::move(declarations.constants);
        for (auto& [palette, list] : declarations.paletteLists) values_.emplace(palette->name, std::move(list));
        palettes_ = std::move(declarations.palettes);
        for (const auto& [palette, value] : palettes_) typer_.registerPaletteType(*palette, value.type);
        current_ = &probeContext_;
        typer_.enterFunction(program_, probeFunction_, false);
    }

    std::optional<Value> known(const Expr& expression, const std::map<std::string, ValueType>& locals) {
        locals_ = &locals;
        std::optional<Value> value;
        try {
            std::set<std::size_t> dependencies;
            inspect(expression, dependencies);
            (void)typer_.typeOf(expression);
            value = evaluate(expression);
        } catch (const SourceError&) { /* not a known value */ }
        locals_ = nullptr;
        return value;
    }

    std::optional<std::string> rejection(const Expr& expression, const std::map<std::string, ValueType>& locals) {
        const auto* call = std::get_if<Call>(&expression);
        if (!call) return std::nullopt;
        locals_ = &locals;
        std::optional<std::string> message;
        try {
            std::set<std::size_t> dependencies;
            inspect(expression, dependencies);
            (void)typer_.typeOf(expression);
            for (const auto* argument : typer_.calls().at(call).arguments)
                if (argument) (void)evaluate(*argument->value);
            try { (void)evaluate(expression); }
            catch (const SourceError& error) { message = error.message; }
        } catch (const SourceError&) { /* an unknown or failing argument */ }
        locals_ = nullptr;
        return message;
    }

    AssetDeclarations resolve() {
        validateNames();
        for (std::size_t i = 0; i < program_.asset.palettes.size(); ++i)
            declarations_.emplace(program_.asset.palettes[i].name, program_.asset.constants.size() + i);
        Function context;
        typer_.enterFunction(program_, context, false);
        std::vector<std::set<std::size_t>> dependencies(declarations_.size());
        std::vector<std::vector<std::size_t>> users(dependencies.size());
        std::vector<std::size_t> pending(dependencies.size());
        std::set<std::size_t> ready;
        for (std::size_t i = 0; i < dependencies.size(); ++i) {
            select(i);
            if (currentPalette_) {
                if (!extendedPalette(*currentPalette_)) (void)resolvePalette(*currentPalette_);
                else {
                    for (const auto& element : currentPalette_->elements) {
                        if (std::holds_alternative<RandomRange>(*element)) inspectCapture(*element, dependencies[i]);
                        else inspect(*element, dependencies[i]);
                    }
                }
            } else {
                if (current_->type.isResource())
                    fail(current_->location, "surface handles depend on generation and cannot be const");
                inspect(*current_->value, dependencies[i]);
            }
            pending[i] = dependencies[i].size();
            if (pending[i] == 0) ready.insert(i);
            for (const auto dependency : dependencies[i]) users[dependency].push_back(i);
        }
        // Topological evaluation avoids recursion through arbitrarily long
        // chains of declarations; expression recursion stays parser-bounded.
        while (!ready.empty()) {
            const auto i = *ready.begin();
            ready.erase(ready.begin());
            select(i);
            if (currentPalette_) {
                auto palette = evaluatePalette(*currentPalette_);
                auto data = std::make_shared<ListData>(ListData{ValueType::list(palette.type), palette.elements});
                values_.emplace(currentPalette_->name, ListValue{data->type, std::move(data)});
                palettes_.emplace(currentPalette_, std::move(palette));
            } else {
                const auto actual = typer_.typeOf(*current_->value, current_->type);
                const auto expected = valueTypeOf(current_->type);
                if (!argFits(expected, actual)) {
                    SourceError error{locationOf(*current_->value), "const '" + current_->name + "' needs a " +
                        typeName(expected) + ", got " + typeName(actual), {}, DiagnosticCode::TypeError, DiagnosticStage::Typing};
                    error.expectedType = typeName(expected); error.actualType = typeName(actual);
                    throw error;
                }
                Value value = evaluate(*current_->value);
                if (current_->type == ParamType::F32 && std::holds_alternative<i32>(value)) value = number(value);
                values_.emplace(current_->name, value);
            }
            for (const auto user : users[i]) if (--pending[user] == 0) ready.insert(user);
        }
        if (values_.size() != dependencies.size()) {
            std::size_t i = 0;
            while (pending[i] == 0) ++i;
            std::map<std::size_t, std::size_t> visited;
            std::vector<std::size_t> path;
            while (!visited.contains(i)) {
                visited.emplace(i, path.size()); path.push_back(i);
                for (const auto dependency : dependencies[i]) if (pending[dependency] != 0) { i = dependency; break; }
            }
            std::string cycle;
            for (std::size_t at = visited.at(i); at < path.size(); ++at)
                cycle += declarationName(path[at]) + " -> ";
            cycle += declarationName(i);
            select(path.back());
            throw SourceError{currentPalette_ ? currentPalette_->location : current_->location,
                "cyclic const dependency: " + cycle, {}, DiagnosticCode::AssetConstantCycle, DiagnosticStage::Typing};
        }
        AssetDeclarations result;
        for (const auto& constant : program_.asset.constants) result.constants.emplace(constant.name, values_.at(constant.name));
        for (const auto& palette : program_.asset.palettes)
            result.paletteLists.emplace(&palette, std::get<ListValue>(values_.at(palette.name)));
        result.palettes = std::move(palettes_);
        return result;
    }

private:
    const Program& program_;
    const std::map<std::string, PartInfo> parts_;
    Typer typer_;
    std::map<std::string, std::size_t> declarations_;
    AssetConstants values_;
    const Asset::Constant* current_ = nullptr;
    const PaletteDecl* currentPalette_ = nullptr;
    std::map<const PaletteDecl*, ResolvedPalette> palettes_;
    // Probe state: the anonymous context and the names shadowing declarations.
    Asset::Constant probeContext_{};
    Function probeFunction_{};
    const std::map<std::string, ValueType>* locals_ = nullptr;

    void registerDeclarations() {
        for (std::size_t i = 0; i < program_.asset.constants.size(); ++i)
            declarations_.emplace(program_.asset.constants[i].name, i);
        for (std::size_t i = 0; i < program_.asset.palettes.size(); ++i)
            declarations_.emplace(program_.asset.palettes[i].name, program_.asset.constants.size() + i);
    }
    void select(std::size_t index) {
        current_ = nullptr; currentPalette_ = nullptr;
        if (index < program_.asset.constants.size()) current_ = &program_.asset.constants[index];
        else currentPalette_ = &program_.asset.palettes[index - program_.asset.constants.size()];
    }
    const std::string& declarationName(std::size_t index) const {
        return index < program_.asset.constants.size() ? program_.asset.constants[index].name :
            program_.asset.palettes[index - program_.asset.constants.size()].name;
    }

    // Declaration diagnostics name their declaration; probes have none.
    [[noreturn]] void fail(const SourceLocation& at, std::string message,
                          DiagnosticCode code = DiagnosticCode::AssetConstantEffect) const {
        if (currentPalette_) message = "palette '" + currentPalette_->name + "': " + message;
        else if (current_ != &probeContext_) message = "const '" + current_->name + "': " + message;
        throw SourceError{at, std::move(message), {}, code, DiagnosticStage::Typing};
    }

    void validateNames() {
        const auto collision = [](const auto& declarations, const std::string& name) {
            return std::any_of(declarations.begin(), declarations.end(), [&](const auto& d) { return d.name == name; });
        };
        for (std::size_t i = 0; i < program_.asset.constants.size(); ++i) {
            current_ = &program_.asset.constants[i];
            const auto& name = current_->name;
            if (!declarations_.emplace(name, i).second) fail(current_->location, "duplicate constant name", DiagnosticCode::TypeError);
            if (name == "seed" || name == "at" || name == "rot" ||
                name == "mirror_x" || name == "mirror_y") fail(current_->location, "reserved name", DiagnosticCode::TypeError);
            if (collision(program_.asset.inputs, name) || collision(program_.asset.outputs, name) ||
                collision(program_.asset.palettes, name) || collision(program_.asset.functions, name) ||
                std::any_of(program_.imports.begin(), program_.imports.end(), [&](const auto& d) { return d.alias == name; }))
                fail(current_->location, "name collides with an asset declaration", DiagnosticCode::TypeError);
            for (const auto& function : program_.asset.functions)
                for (const auto& parameter : function.params) if (parameter.name == name)
                    fail(parameter.location, "parameter cannot shadow an asset constant", DiagnosticCode::TypeError);
        }
    }

    void inspect(const Expr& expression, std::set<std::size_t>& dependencies) {
        if (const auto* id = std::get_if<Identifier>(&expression)) {
            const auto found = declarations_.find(id->name);
            const bool shadowed = locals_ && locals_->contains(id->name);
            if (found != declarations_.end() && !shadowed) { dependencies.insert(found->second); return; }
            std::vector<std::string> names;
            for (const auto& [name, index] : declarations_) { (void)index; names.push_back(name); }
            fail(id->location, "cannot read '" + id->name + "'; only asset constants are allowed" + didYouMean(id->name, names));
        } else if (const auto* call = std::get_if<Call>(&expression)) {
            const auto name = callName(*call->callee);
            if (name == "ramp") {
                const auto bound = bindCall(*call, rampCallSignature(), name);
                const auto* palette = std::get_if<Identifier>(bound.arguments[0]->value.get());
                if (!palette) fail(call->location, "ramp needs a palette name");
                (void)findPalette(program_, *palette);
                inspect(*bound.arguments[0]->value, dependencies);
                inspect(*bound.arguments[1]->value, dependencies);
                return;
            }
            const auto found = builtinSpecs().find(name);
            const bool helper = builtinYieldsToHelper(name) && std::any_of(program_.asset.functions.begin(),
                program_.asset.functions.end(), [&](const auto& function) { return function.name == name; });
            if (found == builtinSpecs().end() || helper) fail(call->location, "user helper and part calls are not allowed ('" + name + "')");
            const auto& spec = found->second;
            if (std::none_of(spec.overloads.begin(), spec.overloads.end(), [&](const auto& o) { return constantBuiltin(spec, o); }))
                fail(call->location, "'" + name + "' has RNG or generation effects and is not allowed");
            for (const auto& argument : call->args) inspect(*argument.value, dependencies);
        } else if (const auto* member = std::get_if<Member>(&expression)) {
            if (surfaceMode(expression)) return;
            inspect(*member->object, dependencies);
        } else if (const auto* index = std::get_if<Index>(&expression)) {
            inspect(*index->object, dependencies);
            inspect(*index->index, dependencies);
        } else if (const auto* binary = std::get_if<Binary>(&expression)) {
            inspect(*binary->left, dependencies); inspect(*binary->right, dependencies);
        } else if (const auto* unary = std::get_if<Unary>(&expression)) {
            inspect(*unary->operand, dependencies);
        } else if (const auto* vector = std::get_if<VectorLit>(&expression)) {
            for (const auto& element : vector->elements) inspect(*element, dependencies);
        } else if (const auto* list = std::get_if<ListLit>(&expression)) {
            for (const auto& element : list->elements) inspect(*element, dependencies);
        } else if (const auto* d = std::get_if<PointDistributionLiteral>(&expression)) {
            for (const auto& point : d->points) { inspect(*point.key, dependencies); inspect(*point.value, dependencies); }
        } else if (std::holds_alternative<RandomRange>(expression)) {
            fail(locationOf(expression), "random ranges are not allowed");
        } else if (const auto* capture = std::get_if<SamplerCapture>(&expression)) {
            inspectCapture(*capture->value, dependencies);
        } else if (const auto* method = std::get_if<ListMethod>(&expression)) {
            inspect(*method->receiver, dependencies);
        } else if (std::holds_alternative<SurfaceBlur>(expression)) {
            fail(locationOf(expression), ".blur has generation effects and is not allowed");
        } else if (std::holds_alternative<SamplerPick>(expression)) {
            fail(locationOf(expression), ".pick has RNG or generation effects and is not allowed");
        }
    }

    void inspectCapture(const Expr& expression, std::set<std::size_t>& dependencies) {
        if (const auto* range = std::get_if<RandomRange>(&expression)) {
            inspect(*range->lower, dependencies); inspect(*range->upper, dependencies);
            if (range->distribution) inspect(*range->distribution, dependencies);
        } else inspect(expression, dependencies);
    }

    Value capture(const Expr& expression, bool stepper = false) {
        const auto* range = std::get_if<RandomRange>(&expression);
        auto data = std::make_shared<SamplerData>();
        data->type = stepper ? ValueType::stepper(typer_.typeOf(expression)) : ValueType::sampler(typer_.typeOf(expression));
        data->mode = !range ? SamplerMode::Constant : range->distribution ? SamplerMode::With : SamplerMode::Uniform;
        data->lower = evaluate(range ? *range->lower : expression);
        if (range) data->upper = evaluate(*range->upper);
        if (data->type.item() == ValueType::F32) {
            data->lower = number(data->lower);
            if (range) data->upper = number(data->upper);
        }
        if (range && range->distribution) data->distribution = std::get<Distribution>(evaluate(*range->distribution));
        if (const auto* message = samplerBoundsError(data->type.item(), data->lower, data->upper, range != nullptr))
            fail(locationOf(expression), message, DiagnosticCode::AssetConstantValue);
        if (data->mode == SamplerMode::With && !math::validDistribution(data->distribution))
            fail(locationOf(expression), std::string(stepper ? "stepper" : "sampler") + " distribution is not initialized or invalid", DiagnosticCode::AssetConstantValue);
        const auto type = data->type;
        if (stepper) return StepperValue{type, std::move(data)};
        return SamplerValue{type, std::move(data)};
    }

    ResolvedPalette evaluatePalette(const PaletteDecl& palette) {
        if (!extendedPalette(palette)) return resolvePalette(palette);
        ResolvedPalette result{typer_.paletteElementType(palette), {}};
        for (const auto& element : palette.elements) {
            Value value = std::holds_alternative<RandomRange>(*element) ? Value(capture(*element)) : evaluate(*element);
            if (result.type.isSampler()) {
                if (const auto* sampler = std::get_if<SamplerValue>(&value)) {
                    if (sampler->type != result.type) {
                        auto data = std::make_shared<SamplerData>(*sampler->data);
                        data->type = result.type; data->lower = number(data->lower);
                        if (data->mode != SamplerMode::Constant) data->upper = number(data->upper);
                        value = SamplerValue{result.type, std::move(data)};
                    }
                } else {
                    if (result.type.item() == ValueType::F32) value = number(value);
                    if (const auto* message = samplerBoundsError(result.type.item(), value, Value{}, false))
                        fail(locationOf(*element), message, DiagnosticCode::AssetConstantValue);
                    auto data = std::make_shared<SamplerData>(); data->type = result.type; data->lower = value;
                    value = SamplerValue{result.type, std::move(data)};
                }
            } else if (result.type == ValueType::F32) value = number(value);
            result.elements.push_back(std::move(value));
        }
        return result;
    }

    // Topological evaluation and probes both see every dependency resolved.
    const ResolvedPalette& resolvedPalette(const Identifier& name) {
        return palettes_.at(&findPalette(program_, name));
    }

    Value operation(Opcode opcode, std::vector<Value> args, const SourceLocation& at) const {
        try {
            if (const auto value = detail::evaluateConstantOperation(opcode, args)) return *value;
        } catch (const Trap& error) {
            fail(at, error.what(), DiagnosticCode::AssetConstantValue);
        }
        throw std::logic_error("checked constant operation was not evaluated");
    }

    Value evaluate(const Expr& expression) {
        const auto at = locationOf(expression);
        if (std::holds_alternative<Literal>(expression)) {
            Value value;
            if (!foldExpression(expression, value)) fail(at, "invalid constant literal", DiagnosticCode::AssetConstantValue);
            return value;
        }
        if (const auto* id = std::get_if<Identifier>(&expression)) {
            if (const auto found = values_.find(id->name); found != values_.end()) return found->second;
            const auto& palette = resolvedPalette(*id);
            auto data = std::make_shared<ListData>(ListData{ValueType::list(palette.type), palette.elements});
            return ListValue{data->type, std::move(data)};
        }
        if (const auto* list = std::get_if<ListLit>(&expression)) {
            auto data = std::make_shared<ListData>();
            data->type = typer_.typeOf(expression);
            for (const auto& element : list->elements) {
                Value value = evaluate(*element);
                if (data->type.element == ValueType::F32) value = number(value);
                data->elements.push_back(std::move(value));
            }
            return ListValue{data->type, std::move(data)};
        }
        if (const auto* method = std::get_if<ListMethod>(&expression)) {
            const auto list = std::get<ListValue>(evaluate(*method->receiver));
            try { return transformList(list, method->name == "sort"); }
            catch (const std::invalid_argument& error) { fail(at, error.what(), DiagnosticCode::AssetConstantValue); }
        }
        if (const auto* description = std::get_if<SamplerCapture>(&expression)) {
            return capture(*description->value, description->stepper);
        }
        if (const auto* literal = std::get_if<PointDistributionLiteral>(&expression)) {
            const auto numeric = [](const Value& value) -> DistributionNumber {
                if (const auto* i = std::get_if<i32>(&value)) return *i;
                return std::get<f32>(value);
            };
            auto data = std::make_shared<PointDistributionData>(); data->family = literal->family;
            for (const auto& point : literal->points) data->points.push_back({numeric(evaluate(*point.key)), numeric(evaluate(*point.value))});
            if (const auto* error = math::pointDistributionError(*data)) fail(at, error, DiagnosticCode::AssetConstantValue);
            Distribution d; d.family = literal->family; d.points = std::move(data); return d;
        }
        if (const auto* member = std::get_if<Member>(&expression)) {
            if (const auto* mode = surfaceMode(expression)) return mode->sourceConstant;
            const auto object = evaluate(*member->object);
            if (const auto* v = std::get_if<Vec2>(&object)) return member->name == "x" ? v->x : v->y;
            const auto c = std::get<Color>(object);
            const u8 channels[] = {c.r, c.g, c.b, c.a};
            return i32{channels[*colorChannel(member->name)]};
        }
        if (const auto* vector = std::get_if<VectorLit>(&expression))
            return Vec2{number(evaluate(*vector->elements[0])), number(evaluate(*vector->elements[1]))};
        if (const auto* index = std::get_if<Index>(&expression)) {
            const auto* legacy = typer_.paletteRead(expression);
            const auto object = legacy ? Value{} : evaluate(*index->object);
            const auto i = std::get<i32>(evaluate(*index->index));
            if (legacy) {
                const auto& palette = palettes_.at(legacy);
                return palette.elements[i >= 0 && static_cast<std::size_t>(i) < palette.elements.size() ?
                    static_cast<std::size_t>(i) : palette.elements.size() - 1];
            }
            const auto& list = std::get<ListValue>(object);
            if (i < 0 || static_cast<std::size_t>(i) >= list.data->elements.size()) fail(at, "list index out of range", DiagnosticCode::AssetConstantValue);
            return list.data->elements[static_cast<std::size_t>(i)];
        }
        if (const auto* call = std::get_if<Call>(&expression)) {
            const auto name = callName(*call->callee);
            const auto& bound = typer_.calls().at(call);
            if (name == "ramp") return sampleRamp(resolvedPalette(
                std::get<Identifier>(*bound.arguments[0]->value)), number(evaluate(*bound.arguments[1]->value)));
            const auto& spec = builtinSpecs().at(name);
            std::vector<ValueType> types;
            for (const auto* arg : bound.arguments) types.push_back(typer_.typeOf(*arg->value));
            const auto& overload = *builtinOverload(spec, types);
            std::vector<Value> args(bound.arguments.size());
            for (const auto i : bound.signature.evaluationOrder) {
                args[i] = evaluate(*bound.arguments[i]->value);
                if (overload.params[i].type == ValueType::F32) args[i] = number(args[i]);
            }
            if (spec.lowering == BuiltinLowering::ListLength)
                return static_cast<i32>(std::get<ListValue>(args[0]).data->elements.size());
            if (overload.adaptation != BuiltinAdaptation::Direct) {
                std::vector<Value> stack;
                for (const auto& step : builtinExpansion(overload)) {
                    if (const auto* argument = std::get_if<std::size_t>(&step)) stack.push_back(args[*argument]);
                    else {
                        const auto opcode = std::get<Opcode>(step);
                        const auto count = findOpcode(opcode)->stack->inputCount;
                        std::vector<Value> operands(stack.end() - count, stack.end());
                        stack.resize(stack.size() - count);
                        stack.push_back(operation(opcode, std::move(operands), at));
                    }
                }
                return stack.at(0);
            }
            if (overload.opcode) return operation(*overload.opcode, std::move(args), at);
            if (spec.lowering == BuiltinLowering::Opcode) return args[0]; // identity int/float
            if (spec.lowering == BuiltinLowering::Beta) {
                args.emplace_back(0.f); args.emplace_back(1.f);
                return operation(Opcode::DistributionBeta, std::move(args), at);
            }
            if (spec.lowering == BuiltinLowering::Round) {
                const bool positive = std::get<f32>(args[0]) >= 0.f;
                auto base = operation(positive ? Opcode::F32Floor : Opcode::F32Ceil, {args[0]}, at);
                const auto fraction = positive ? operation(Opcode::F32Sub, {args[0], base}, at) :
                    operation(Opcode::F32Sub, {base, args[0]}, at);
                if (std::get<f32>(fraction) >= 0.5f)
                    base = operation(positive ? Opcode::F32Add : Opcode::F32Sub, {base, 1.f}, at);
                return operation(Opcode::F32ToI32, {base}, at);
            }
            if (spec.lowering == BuiltinLowering::ColorConstructor) {
                const auto channel = [&](std::size_t i) { return static_cast<u8>(std::clamp(std::get<i32>(args[i]), 0, 255)); };
                return Color{channel(0), channel(1), channel(2), args.size() == 4 ? channel(3) : u8{255}};
            }
        }
        if (const auto* unary = std::get_if<Unary>(&expression)) {
            const auto value = evaluate(*unary->operand);
            if (unary->op == Unary::Op::Not) return i32{std::get<i32>(value) == 0};
            if (unary->op == Unary::Op::BitNot) return operation(Opcode::I32Not, {value}, at);
            return operation(std::holds_alternative<Vec2>(value) ? Opcode::Vec2Neg :
                std::holds_alternative<f32>(value) ? Opcode::F32Neg : Opcode::I32Neg, {value}, at);
        }
        const auto& binary = std::get<Binary>(expression);
        Value left = evaluate(*binary.left);
        if (binary.op == Binary::Op::And && std::get<i32>(left) == 0) return i32{0};
        if (binary.op == Binary::Op::Or && std::get<i32>(left) != 0) return i32{1};
        Value right = evaluate(*binary.right);
        if (binary.op == Binary::Op::And || binary.op == Binary::Op::Or) return i32{std::get<i32>(right) != 0};
        const bool vector = std::holds_alternative<Vec2>(left) || std::holds_alternative<Vec2>(right);
        const bool floating = std::holds_alternative<f32>(left) || std::holds_alternative<f32>(right);
        if (vector) {
            if (!std::holds_alternative<Vec2>(left)) { left = number(left); std::swap(left, right); }
            else if (!std::holds_alternative<Vec2>(right)) right = number(right);
        } else if (floating) { left = number(left); right = number(right); }
        Opcode opcode;
        switch (binary.op) {
        case Binary::Op::Add: opcode = vector ? Opcode::Vec2Add : floating ? Opcode::F32Add : Opcode::I32Add; break;
        case Binary::Op::Sub: opcode = vector ? Opcode::Vec2Sub : floating ? Opcode::F32Sub : Opcode::I32Sub; break;
        case Binary::Op::Mul: opcode = vector ? Opcode::Vec2Mul : floating ? Opcode::F32Mul : Opcode::I32Mul; break;
        case Binary::Op::Div: opcode = vector ? Opcode::Vec2Div : floating ? Opcode::F32Div : Opcode::I32DivS; break;
        case Binary::Op::Mod: opcode = Opcode::I32RemS; break;
        case Binary::Op::Eq: opcode = floating ? Opcode::F32Eq : Opcode::I32Eq; break;
        case Binary::Op::Ne: opcode = floating ? Opcode::F32Ne : Opcode::I32Ne; break;
        case Binary::Op::Lt: opcode = floating ? Opcode::F32Lt : Opcode::I32LtS; break;
        case Binary::Op::Le: opcode = floating ? Opcode::F32Le : Opcode::I32LeS; break;
        case Binary::Op::Gt: opcode = floating ? Opcode::F32Gt : Opcode::I32GtS; break;
        case Binary::Op::Ge: opcode = floating ? Opcode::F32Ge : Opcode::I32GeS; break;
        case Binary::Op::Xor: opcode = Opcode::I32Xor; break;
        case Binary::Op::BitAnd: opcode = Opcode::I32And; break;
        case Binary::Op::BitOr: opcode = Opcode::I32Or; break;
        case Binary::Op::Shl: opcode = Opcode::I32Shl; break;
        case Binary::Op::Shr: opcode = Opcode::I32ShrU; break;
        default: throw std::logic_error("constant binary operation");
        }
        return operation(opcode, {left, right}, at);
    }
};
} // namespace

AssetDeclarations resolveAssetDeclarations(const Program& program) { return Resolver(program).resolve(); }
AssetConstants resolveAssetConstants(const Program& program) { return resolveAssetDeclarations(program).constants; }

// The resolver's Typer refers to its own members: construct it in place.
struct ConstantProbe::State {
    Resolver resolver;
    State(const Program& program, AssetDeclarations declarations)
        : resolver(program, std::move(declarations)) {}
};
ConstantProbe::ConstantProbe(const Program& program, AssetDeclarations declarations)
    : state_(std::make_unique<State>(program, std::move(declarations))) {}
ConstantProbe::~ConstantProbe() = default;
std::optional<Value> ConstantProbe::known(const Expr& expression,
                                          const std::map<std::string, ValueType>& locals) {
    return state_->resolver.known(expression, locals);
}
std::optional<std::string> ConstantProbe::rejection(const Expr& call,
                                                    const std::map<std::string, ValueType>& locals) {
    return state_->resolver.rejection(call, locals);
}

} // namespace iprop::ips
