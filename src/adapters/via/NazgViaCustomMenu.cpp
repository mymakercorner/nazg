// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgViaCustomMenu.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>

namespace nazg
{
    size_t ValueWidth(const CustomControl& control)
    {
        using Type = CustomControl::Type;
        switch (control.type)
        {
            case Type::Keycode:
            case Type::Colour:
                return 2;
            case Type::Range:
                return control.maximum > 255 ? 2 : 1;
            case Type::Toggle:
                return std::max(control.off.size(), control.on.size());
            case Type::Label:
                // The frame after the command, the channel, the value id and the extra bytes.
                return c_ViaReportSize - std::min(c_ViaReportSize - 1, 3 + control.extra.size());
            default:
                return 1;
        }
    }

    int RangeValueOf(const CustomControl& control, const std::vector<uint8_t>& value)
    {
        if (control.maximum > 255)
            return value.size() >= 2 ? (value[0] << 8) | value[1] : 0;
        return value.empty() ? 0 : value[0];
    }

    std::vector<uint8_t> RangeBytes(const CustomControl& control, int value)
    {
        value = std::clamp(value, 0, 0xFFFF);
        if (control.maximum > 255)
            return { static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF) };
        return { static_cast<uint8_t>(std::min(value, 255)) };
    }

    bool IsToggledOn(const CustomControl& control, const std::vector<uint8_t>& value)
    {
        if (value.size() < control.on.size())
            return false;
        return std::equal(control.on.begin(), control.on.end(), value.begin());
    }

    std::string TextOf(const std::vector<uint8_t>& value)
    {
        const auto end = std::find(value.begin(), value.end(), uint8_t{ 0 });
        return std::string(value.begin(), end);
    }

    // ------------------------------------------------------------------------------------------
    // Conditions, as pelpi reads them: the loosest operator splits first, at its first place.

    namespace
    {
        struct Token
        {
            enum class Kind : uint8_t
            {
                Number,
                Value,      // {name} or {name.byte}
                Operator,
                Not,
                Open,
                Close,
            };

            Kind        kind = Kind::Number;
            int64_t     number = 0;
            std::string text;   // the value's name, or the operator
            size_t      byte = 0;
        };

        struct BadCondition
        {
        };

        bool IsNameCharacter(char c)
        {
            return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-' || c == '[' || c == ']';
        }

        std::vector<Token> Tokenize(std::string_view text)
        {
            std::vector<Token> tokens;
            size_t             at = 0;
            while (at < text.size())
            {
                const char c = text[at];
                if (std::isspace(static_cast<unsigned char>(c)) != 0)
                {
                    ++at;
                    continue;
                }
                Token token;
                if (std::isdigit(static_cast<unsigned char>(c)) != 0)
                {
                    token.kind = Token::Kind::Number;
                    while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) != 0)
                        token.number = token.number * 10 + (text[at++] - '0');
                }
                else if (c == '{')
                {
                    token.kind = Token::Kind::Value;
                    ++at;
                    while (at < text.size() && IsNameCharacter(text[at]))
                        token.text += text[at++];
                    if (at < text.size() && text[at] == '.')
                    {
                        ++at;
                        if (at >= text.size() || std::isdigit(static_cast<unsigned char>(text[at])) == 0)
                            throw BadCondition();
                        while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) != 0)
                            token.byte = token.byte * 10 + static_cast<size_t>(text[at++] - '0');
                    }
                    if (at >= text.size() || text[at] != '}' || token.text.empty())
                        throw BadCondition();
                    ++at;
                }
                else if (c == '(' || c == ')')
                {
                    token.kind = c == '(' ? Token::Kind::Open : Token::Kind::Close;
                    ++at;
                }
                else
                {
                    const std::string_view two = text.substr(at, 2);
                    if (two == "||" || two == "&&" || two == "==" || two == "!=" || two == "<=" || two == ">=")
                    {
                        token.kind = Token::Kind::Operator;
                        token.text = std::string(two);
                        at += 2;
                    }
                    else if (c == '<' || c == '>')
                    {
                        token.kind = Token::Kind::Operator;
                        token.text = std::string(1, c);
                        ++at;
                    }
                    else if (c == '!')
                    {
                        token.kind = Token::Kind::Not;
                        ++at;
                    }
                    else
                        throw BadCondition();
                }
                tokens.push_back(std::move(token));
            }
            return tokens;
        }

        // Loosest first, as pelpi's findSplitOp ranks them.
        constexpr std::string_view c_Operators[] = { "||", "&&", "!=", "==", ">=", ">", "<=", "<" };

        class Evaluator
        {
        public:
            Evaluator(const CustomValues& values, uint32_t firmwareVersion)
                : m_Values(values), m_FirmwareVersion(firmwareVersion)
            {
            }

            int64_t Evaluate(const std::vector<Token>& tokens, size_t begin, size_t end) const
            {
                if (begin >= end)
                    throw BadCondition();

                for (const std::string_view op : c_Operators)
                {
                    int depth = 0;
                    for (size_t at = begin; at < end; ++at)
                    {
                        const Token& token = tokens[at];
                        if (token.kind == Token::Kind::Open)
                            ++depth;
                        else if (token.kind == Token::Kind::Close)
                            --depth;
                        else if (depth == 0 && token.kind == Token::Kind::Operator && token.text == op)
                        {
                            const int64_t left  = Evaluate(tokens, begin, at);
                            const int64_t right = Evaluate(tokens, at + 1, end);
                            return Apply(op, left, right);
                        }
                    }
                    if (depth != 0)
                        throw BadCondition();
                }

                // No operator outside brackets: one term, perhaps negated, perhaps bracketed.
                const Token& first = tokens[begin];
                if (first.kind == Token::Kind::Not)
                    return Evaluate(tokens, begin + 1, end) == 0 ? 1 : 0;
                if (first.kind == Token::Kind::Open && tokens[end - 1].kind == Token::Kind::Close)
                    return Evaluate(tokens, begin + 1, end - 1);
                if (end - begin != 1)
                    throw BadCondition();
                if (first.kind == Token::Kind::Number)
                    return first.number;
                if (first.kind == Token::Kind::Value)
                    return ValueOf(first.text, first.byte);
                throw BadCondition();
            }

        private:
            static int64_t Apply(std::string_view op, int64_t left, int64_t right)
            {
                if (op == "||")
                    return left != 0 || right != 0;
                if (op == "&&")
                    return left != 0 && right != 0;
                if (op == "!=")
                    return left != right;
                if (op == "==")
                    return left == right;
                if (op == ">=")
                    return left >= right;
                if (op == ">")
                    return left > right;
                if (op == "<=")
                    return left <= right;
                return left < right;
            }

            int64_t ValueOf(const std::string& name, size_t byte) const
            {
                if (name == c_FirmwareVersionName)
                    return byte == 0 ? static_cast<int64_t>(m_FirmwareVersion) : 0;
                const auto found = m_Values.find(name);
                if (found == m_Values.end() || byte >= found->second.size())
                    return 0;
                return found->second[byte];
            }

            const CustomValues& m_Values;
            uint32_t            m_FirmwareVersion;
        };
    }

    bool ConditionHolds(std::string_view condition, const CustomValues& values, uint32_t firmwareVersion)
    {
        try
        {
            const std::vector<Token> tokens = Tokenize(condition);
            return Evaluator(values, firmwareVersion).Evaluate(tokens, 0, tokens.size()) != 0;
        }
        catch (const BadCondition&)
        {
            return true;
        }
    }

    bool ConditionsHold(const std::vector<std::string>& conditions, const CustomValues& values, uint32_t firmwareVersion)
    {
        return std::all_of(conditions.begin(), conditions.end(), [&](const std::string& condition)
                           { return ConditionHolds(condition, values, firmwareVersion); });
    }

    bool IsReadableCondition(std::string_view condition)
    {
        try
        {
            const std::vector<Token> tokens = Tokenize(condition);
            Evaluator(CustomValues{}, 0).Evaluate(tokens, 0, tokens.size());
            return true;
        }
        catch (const BadCondition&)
        {
            return false;
        }
    }

    std::vector<std::string> NamesIn(std::string_view condition)
    {
        std::vector<std::string> names;
        try
        {
            for (const Token& token : Tokenize(condition))
                if (token.kind == Token::Kind::Value && std::find(names.begin(), names.end(), token.text) == names.end())
                    names.push_back(token.text);
        }
        catch (const BadCondition&)
        {
        }
        return names;
    }

    // ------------------------------------------------------------------------------------------
    // Constraints, as the VIA app's range-constraints.ts resolves them.

    namespace
    {
        using Operation = RangeConstraint::Operation;

        bool IsLower(Operation operation)   // the constraint gives a minimum
        {
            return operation == Operation::Greater || operation == Operation::GreaterOrEqual;
        }

        int Boundary(const RangeConstraint& constraint, int reference)
        {
            const int value = reference + constraint.offset;
            if (constraint.operation == Operation::Greater)
                return value + 1;
            if (constraint.operation == Operation::Less)
                return value - 1;
            return value;
        }

        bool Violates(int value, const RangeConstraint& constraint, int reference)
        {
            const int compared = reference + constraint.offset;
            switch (constraint.operation)
            {
                case Operation::Greater:
                    return value <= compared;
                case Operation::GreaterOrEqual:
                    return value < compared;
                case Operation::Less:
                    return value >= compared;
                default:
                    return value > compared;
            }
        }

        const CustomControl* RangeNamed(const std::vector<const CustomControl*>& ranges, std::string_view name)
        {
            for (const CustomControl* range : ranges)
                if (range->name == name)
                    return range;
            return nullptr;
        }

        std::map<std::string, int> RangeValues(const std::vector<const CustomControl*>& ranges, const CustomValues& values)
        {
            std::map<std::string, int> numbers;
            for (const CustomControl* range : ranges)
                if (const auto found = values.find(range->name); found != values.end())
                    numbers[range->name] = RangeValueOf(*range, found->second);
            return numbers;
        }
    }

    std::map<std::string, int> ResolveRange(const std::vector<const CustomControl*>& ranges, const CustomValues& values,
                                            const std::string& name, int requested)
    {
        const std::map<std::string, int> before = RangeValues(ranges, values);
        std::map<std::string, int>       after  = before;
        std::set<std::string>            moving;

        const std::function<void(const std::string&, int, const std::string&)> resolve =
            [&](const std::string& id, int value, const std::string& ignored)
        {
            const CustomControl* control = RangeNamed(ranges, id);
            if (!control)
                return;
            after[id] = std::clamp(value, control->minimum, control->maximum);
            moving.insert(id);
            for (const RangeConstraint& constraint : control->constraints)
            {
                const auto other = after.find(constraint.reference);
                if (constraint.reference == ignored || other == after.end() ||
                    !Violates(after[id], constraint, other->second))
                    continue;
                if (constraint.push && moving.count(constraint.reference) == 0)
                {
                    const int strict = constraint.operation == Operation::Greater ? -1
                                     : constraint.operation == Operation::Less    ? 1
                                                                                   : 0;
                    resolve(constraint.reference, after[id] - constraint.offset + strict, id);
                }
                const int boundary = Boundary(constraint, after[constraint.reference]);
                after[id] = IsLower(constraint.operation) ? std::max(after[id], boundary) : std::min(after[id], boundary);
            }
            moving.erase(id);
        };
        resolve(name, requested, std::string());

        std::map<std::string, int> changed;
        for (const auto& [id, value] : after)
            if (const auto was = before.find(id); was == before.end() || was->second != value)
                changed[id] = value;
        return changed;
    }

    std::pair<int, int> RangeBounds(const CustomControl& control, const std::vector<const CustomControl*>& ranges,
                                    const CustomValues& values)
    {
        int                              lowest = control.minimum, highest = control.maximum;
        const std::map<std::string, int> numbers = RangeValues(ranges, values);
        for (const RangeConstraint& constraint : control.constraints)
        {
            const auto other = numbers.find(constraint.reference);
            if (constraint.push || other == numbers.end())
                continue;
            const int boundary = Boundary(constraint, other->second);
            if (IsLower(constraint.operation))
                lowest = std::max(lowest, boundary);
            else
                highest = std::min(highest, boundary);
        }
        return { lowest, std::max(lowest, highest) };
    }

    // ------------------------------------------------------------------------------------------
    // The wire: the value after the extra bytes, which the reply echoes.

    Task<std::vector<uint8_t>> ReadCustomValue(ViaProtocol& via, const CustomControl& control)
    {
        const size_t         width = ValueWidth(control);
        std::vector<uint8_t> reply = co_await via.GetCustomValue(control.channel, control.valueId, control.extra,
                                                                 control.extra.size() + width);
        reply.erase(reply.begin(), reply.begin() + std::min(reply.size(), control.extra.size()));
        co_return reply;
    }

    Task<void> WriteCustomValue(ViaProtocol& via, const CustomControl& control, const std::vector<uint8_t>& value)
    {
        std::vector<uint8_t> bytes = control.extra;
        bytes.insert(bytes.end(), value.begin(), value.end());
        co_await via.SetCustomValue(control.channel, control.valueId, bytes);
    }
}
