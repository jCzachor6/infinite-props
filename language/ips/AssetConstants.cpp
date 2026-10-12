#include "ips/AssetConstants.hpp"
#include "ips/Fold.hpp"
#include "ips/Typing.hpp"
#include "ips/Suggestions.hpp"
#include "iprop/VmInternals.hpp"

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

    AssetConstants resolve() {
        validateNames();
        Function context;
        typer_.enterFunction(program_, context, false);
        std::vector<std::set<std::size_t>> dependencies(program_.asset.constants.size());
        std::vector<std::vector<std::size_t>> users(dependencies.size());
        std::vector<std::size_t> pending(dependencies.size());
        std::set<std::size_t> ready;
        for (std::size_t i = 0; i < dependencies.size(); ++i) {
            current_ = &program_.asset.constants[i];
            inspect(*current_->value, dependencies[i]);
            const auto actual = typer_.typeOf(*current_->value, current_->type);
            const auto expected = valueTypeOf(current_->type);
            if (!argFits(expected, actual)) {
                SourceError error{locationOf(*current_->value), "const '" + current_->name + "' needs a " +
                    typeName(expected) + ", got " + typeName(actual), {}, DiagnosticCode::TypeError, DiagnosticStage::Typing};
                error.expectedType = typeName(expected);
                error.actualType = typeName(actual);
                throw error;
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
            current_ = &program_.asset.constants[i];
            Value value = evaluate(*current_->value);
            if (current_->type == ParamType::F32 && std::holds_alternative<i32>(value)) value = number(value);
            values_.emplace(current_->name, value);
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
                cycle += program_.asset.constants[path[at]].name + " -> ";
            cycle += program_.asset.constants[i].name;
            throw SourceError{program_.asset.constants[path.back()].location,
                "cyclic const dependency: " + cycle, {}, DiagnosticCode::AssetConstantCycle, DiagnosticStage::Typing};
        }
        return std::move(values_);
    }

private:
    const Program& program_;
    const std::map<std::string, PartInfo> parts_;
    Typer typer_;
    std::map<std::string, std::size_t> declarations_;
    AssetConstants values_;
    const Asset::Constant* current_ = nullptr;

    [[noreturn]] void fail(const SourceLocation& at, std::string message,
                          DiagnosticCode code = DiagnosticCode::AssetConstantEffect) const {
        throw SourceError{at, "const '" + current_->name + "': " + std::move(message), {}, code, DiagnosticStage::Typing};
    }

    void validateNames() {
        const auto collision = [](const auto& declarations, const std::string& name) {
            return std::any_of(declarations.begin(), declarations.end(), [&](const auto& d) { return d.name == name; });
        };
        for (std::size_t i = 0; i < program_.asset.constants.size(); ++i) {
            current_ = &program_.asset.constants[i];
            const auto& name = current_->name;
            if (!declarations_.emplace(name, i).second) fail(current_->location, "duplicate constant name", DiagnosticCode::TypeError);
            if (name == "seed" || name == "v" || name == "a" || name == "at" || name == "rot" ||
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
            if (found != declarations_.end()) { dependencies.insert(found->second); return; }
            for (const auto& palette : program_.asset.palettes) if (palette.name == id->name) return;
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
            inspect(*member->object, dependencies);
        } else if (const auto* index = std::get_if<Index>(&expression)) {
            const auto* name = std::get_if<Identifier>(index->object.get());
            const bool palette = name && std::any_of(program_.asset.palettes.begin(), program_.asset.palettes.end(), [&](const auto& p) { return p.name == name->name; });
            if (!palette) inspect(*index->object, dependencies);
            inspect(*index->index, dependencies);
        } else if (const auto* binary = std::get_if<Binary>(&expression)) {
            inspect(*binary->left, dependencies); inspect(*binary->right, dependencies);
        } else if (const auto* unary = std::get_if<Unary>(&expression)) {
            inspect(*unary->operand, dependencies);
        } else if (const auto* vector = std::get_if<VectorLit>(&expression)) {
            for (const auto& element : vector->elements) inspect(*element, dependencies);
        } else if (const auto* list = std::get_if<ListLit>(&expression)) {
            for (const auto& element : list->elements) inspect(*element, dependencies);
        } else if (std::holds_alternative<RandomRange>(expression)) {
            fail(locationOf(expression), "random ranges are not allowed");
        }
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
            const auto palette = resolvePalette(findPalette(program_, *id));
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
        if (const auto* member = std::get_if<Member>(&expression)) {
            const auto object = evaluate(*member->object);
            if (const auto* v = std::get_if<Vec2>(&object)) return member->name == "x" ? v->x : v->y;
            const auto c = std::get<Color>(object);
            const u8 channels[] = {c.r, c.g, c.b, c.a};
            return i32{channels[*colorChannel(member->name)]};
        }
        if (const auto* vector = std::get_if<VectorLit>(&expression))
            return Vec2{number(evaluate(*vector->elements[0])), number(evaluate(*vector->elements[1]))};
        if (const auto* index = std::get_if<Index>(&expression)) {
            const auto* name = std::get_if<Identifier>(index->object.get());
            const bool legacy = name && std::any_of(program_.asset.palettes.begin(), program_.asset.palettes.end(), [&](const auto& p) { return p.name == name->name; });
            const auto object = legacy ? Value{} : evaluate(*index->object);
            const auto i = std::get<i32>(evaluate(*index->index));
            if (legacy) {
                const auto palette = resolvePalette(findPalette(program_, *name));
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
            if (name == "ramp") return sampleRamp(resolvePalette(findPalette(program_,
                std::get<Identifier>(*bound.arguments[0]->value))), number(evaluate(*bound.arguments[1]->value)));
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

AssetConstants resolveAssetConstants(const Program& program) { return Resolver(program).resolve(); }

} // namespace iprop::ips
