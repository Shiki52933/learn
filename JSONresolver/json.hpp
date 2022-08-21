#pragma once
#define BOOST_VARIANT_NO_FULL_RECURSIVE_VARIANT_SUPPORT
#include <string>
#include <map>
#include <vector>
#include <boost/variant.hpp>


struct JSONNullType {};

typedef boost::make_recursive_variant<
                    std::string,
                    double,
                    bool,
                    JSONNullType,
                    std::map<std::string, boost::recursive_variant_>,
                    std::vector<boost::recursive_variant_>
                    >::type JSONBasicValue;  // 基本数据类型

typedef std::vector<JSONBasicValue> JSONBasicArray;
typedef std::map<std::string, JSONBasicValue> JSONBasicObject;

// JSON基本数据访问器，返回JSON基本数据的字符串表示
struct JSONBasicValueVisitor: public boost::static_visitor<std::string>{
    std::string operator()(const std::string& s);
    
    std::string operator()(const double& value);

    std::string operator()(const bool& value);

    std::string operator()(const JSONNullType& value);

    std::string operator()(const JSONBasicObject& value);

    std::string operator()(const JSONBasicArray& array);
     
};

extern JSONBasicValueVisitor visitor;

JSONBasicValue resolve_JSON_file(const std::string& filepath);

extern std::string to_string(const JSONBasicValue& value);