#pragma once
#include <vector>
#include <exception>
#include <unordered_set>
#include <unordered_map>

#if __has_include(<rapidjson/writer.h>)
#include <rapidjson/writer.h>
#include <rapidjson/prettywriter.h>
#endif

/*
namespace rapidjson {
    template<typename OutputStream>
    PrettyWriter(OutputStream&) -> PrettyWriter<OutputStream, UTF8<>, UTF8<>, CrtAllocator, kWriteDefaultFlags>;
    template<typename OutputStream, typename StackAllocator>
    PrettyWriter(OutputStream&, StackAllocator*) -> PrettyWriter<OutputStream, UTF8<>, UTF8<>, StackAllocator, kWriteDefaultFlags>;
    template<typename OutputStream, typename StackAllocator>
    PrettyWriter(OutputStream&, StackAllocator*, size_t) -> PrettyWriter<OutputStream, UTF8<>, UTF8<>, StackAllocator, kWriteDefaultFlags>;

    template<typename OutputStream>
    Writer(OutputStream&) -> Writer<OutputStream, UTF8<>, UTF8<>, CrtAllocator, kWriteDefaultFlags>;
    template<typename OutputStream, typename StackAllocator>
    Writer(OutputStream&, StackAllocator*) -> Writer<OutputStream, UTF8<>, UTF8<>, StackAllocator, kWriteDefaultFlags>;
    template<typename OutputStream, typename StackAllocator>
    Writer(OutputStream&, StackAllocator*, size_t) -> Writer<OutputStream, UTF8<>, UTF8<>, StackAllocator, kWriteDefaultFlags>;
}  // namespace rapidjson
*/

namespace thxx::parser {

    class ArgParserError : public std::invalid_argument {
    public:
        using std::invalid_argument::invalid_argument;
    };


    enum class TypeTag {
        Unknown = 0,
        Bool,
        Int,
        Float,
        String,
        BoolVector,
        IntVector,
        FloatVector,
        StringVector
    };

    template <typename T>
    constexpr bool is_vector = std::is_same_v<std::decay_t<T>,
                                              std::vector< typename T::value_type,
                                                           typename T::allocator_type > >;

    template <typename T>
    constexpr TypeTag vector_tag_of =
        std::is_same_v<std::decay_t<T>, bool> ? TypeTag::Bool :
        std::is_integral_v<T> ? TypeTag::IntVector :
        std::is_floating_point_v<T> ? TypeTag::FloatVector :
        TypeTag::StringVector;

    template <typename T>
    constexpr TypeTag type_tag_of =
        // is_vector<T> ?  vector_tag_of<T> :
        std::is_same_v<std::decay_t<T>, bool> ? TypeTag::Bool :
        std::is_integral_v<T> ? TypeTag::Int :
        std::is_floating_point_v<T> ? TypeTag::Float :
        std::is_same_v<std::decay_t<T>, std::string> ? TypeTag::String :
        TypeTag::Unknown;


    struct ArgValue {
        std::string key;
        std::vector<std::string> value;
        TypeTag type = TypeTag::Unknown;
    };

    struct ArgParser {
        using Value = std::vector<std::string>;

        std::unordered_set<std::string> keys;
        std::unordered_map<std::string, ArgValue> parsed_map;

        ArgParser(int argc, char *argv[]) {
            std::string key;
            std::vector<std::string> value;
            for (int i = 1; i < argc; ++i) {
                std::string s = argv[i];
                if (s.find("--") == 0) {
                    key = s;
                    value.clear();
                    value.shrink_to_fit();
                } else {
                    value.push_back(s);
                    parsed_map[key] = {key, value};
                }
            }
        }

        TypeTag assign(const Value& src, bool& dst) const {
            const auto& v = src[0];
            if (v != "true" && v != "false") {
                throw ArgParserError("bool value should be \"true\" or \"false\" but passed: \"" + v + "\"");
            }
            dst = v == "true";
            return TypeTag::Bool;
        }

        TypeTag assign(const Value& src, std::string& dst) const {
            dst = src[0];
            return TypeTag::String;
        }

        template <typename T>
        std::enable_if_t<std::is_integral_v<T>, TypeTag> assign(const Value& src, T& dst) const {
            dst = std::stoi(src[0]);
            return TypeTag::Int;
        }

        template <typename T>
        std::enable_if_t<std::is_floating_point_v<T>, TypeTag> assign(const Value& src, T& dst) const {
            dst = std::stod(src[0]);
            return TypeTag::Float;
        }

        template <typename T, typename Alloc>
        TypeTag assign(const Value& src, std::vector<T, Alloc>& dst) const {
            dst.resize(src.size());
            for (size_t i = 0; i < src.size(); ++i) {
                assign({src[i]}, dst[i]);
            }
            return vector_tag_of<T>;
        }

        template <typename T>
        void set_default_value(const std::string& key, const T& v) {
            this->parsed_map[key] = {key, {std::to_string(v)}, type_tag_of<T>};
        }


        void set_default_value(const std::string& key, const bool& v) {
            this->parsed_map[key] = {key, {v ? "true" : "false"}, TypeTag::Bool};
        }

        void set_default_value(const std::string& key, const std::string& v) {
            this->parsed_map[key] = {key, {v}, TypeTag::String};
        }

        template <typename T>
        void set_default_value(const std::string& key, const std::vector<T>& v) {
            std::vector<std::string> val;
            val.reserve(v.size());
            for (auto&& u: v) {
                val.push_back(std::to_string(u));
            }
            this->parsed_map[key] = {key, val, vector_tag_of<T>};
        }

        template <typename T>
        void add(const std::string& key, T& value, bool required=false) {
            if (key.find("--") != 0) {
                throw ArgParserError("key should start with \"--\" but \"" + key + "\".");
            }
            this->keys.insert(key);
            if (this->parsed_map.find(key) != this->parsed_map.end()) {
                try {
                    parsed_map[key].type = assign(parsed_map[key].value, value);
                    return;
                } catch (std::invalid_argument& e) {
                    throw ArgParserError(std::string(e.what()) + "\nthrown from key: " + key);
                }
            }
            else if (required) {
                auto msg = "cmd arg: \"" + key + "\" is required but not found.";
                throw ArgParserError(msg);
            }
            else {
                this->set_default_value(key, value);
            }
        }

        template <typename T>
        void required(const std::string& key, T& value) {
            add(key, value, true);
        }

        /// throw if an invalid argument (key) are passed
        void check() {
            std::string invalid;
            for (const auto& kv : this->parsed_map) {
                if (this->keys.find(kv.first) == this->keys.end()) {
                    invalid += kv.first +  ", ";
                }
            }
            if (invalid.empty()) return;

            auto msg = "cmd args: [" + invalid.substr(0, invalid.size() - 2) + "] are found but not defined.";
            throw ArgParserError(msg);
        }

#if __has_include(<rapidjson/writer.h>)
        // TODO from JSON
        void from_json(std::string json) {
            
        }

        std::string to_json(bool pretty=true, bool single_line_array=true) const {
            if (pretty) {
                if (single_line_array) {
                    return to_json_impl<true, true>();
                }
                return to_json_impl<true, false>();
            }
            return to_json_impl<false, false>();
        }

        template <bool pretty, bool single_line_array>
        std::string to_json_impl() const {

            rapidjson::StringBuffer s;
            std::conditional_t<
                pretty,
                rapidjson::PrettyWriter<rapidjson::StringBuffer>,
                rapidjson::Writer<rapidjson::StringBuffer>>
                writer(s);

            if constexpr (pretty) {
                    writer.SetFormatOptions(single_line_array ? rapidjson::kFormatSingleLineArray : rapidjson::kFormatDefault);
            }

            writer.StartObject();
            for (const auto& kv : this->parsed_map) {
                writer.Key(kv.first.c_str());
                switch (kv.second.type) {
                case TypeTag::Int:
                {
                    std::int64_t i;
                    assign(kv.second.value, i);
                    writer.Int(i);
                    break;
                }
                case TypeTag::Float:
                {
                    double d;
                    assign(kv.second.value, d);
                    writer.Double(d);
                    break;
                }
                case TypeTag::Bool:
                {
                    bool b;
                    assign(kv.second.value, b);
                    writer.Bool(b);
                    break;
                }
                case TypeTag::IntVector:
                {
                    std::vector<std::int64_t> iv;
                    assign(kv.second.value, iv);
                    writer.StartArray();
                    for (auto i : iv) {
                        writer.Int(i);
                    }
                    writer.EndArray();
                    break;
                }
                case TypeTag::FloatVector:
                {
                    std::vector<double> iv;
                    assign(kv.second.value, iv);
                    writer.StartArray();
                    for (auto i : iv) {
                        writer.Double(i);
                    }
                    writer.EndArray();
                    break;
                }
                case TypeTag::StringVector:
                {
                    std::vector<std::string> iv;
                    assign(kv.second.value, iv);
                    writer.StartArray();
                    for (auto i : iv) {
                        writer.String(i.c_str());
                    }
                    writer.EndArray();
                    break;
                }
                default:
                    writer.String(kv.second.value[0].c_str());
                }
            }
            writer.EndObject();
            return s.GetString();
        }
#endif // __has_include(<rapidjson/writer.h>)
    };

}