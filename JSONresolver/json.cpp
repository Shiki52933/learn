#pragma once
#include "json.hpp"
#include <boost/lexical_cast.hpp>
#include <fstream>
#include <functional>
#include <filesystem>
#include <exception>

JSONBasicValueVisitor visitor;

std::string to_string(const JSONBasicValue& value){
    return boost::apply_visitor(visitor, value);
}

std::string string_JSON_object(const JSONBasicObject::value_type& val);
std::string string_JSON_array(const JSONBasicValue& val);

// JSON基本数据访问器，返回JSON基本数据的字符串表示
std::string JSONBasicValueVisitor::operator()(const std::string& s){
    return "\""+s+"\"";
}

std::string JSONBasicValueVisitor::operator()(const double& value){
    return boost::lexical_cast<std::string>(value);
}

std::string JSONBasicValueVisitor::operator()(const bool& value){
    return value ? "true" : "false";
}

std::string JSONBasicValueVisitor::operator()(const JSONNullType& value){
    return "null";
}

std::string JSONBasicValueVisitor::operator()(const JSONBasicObject& value){
    std::string result="{ ";

    if(!value.empty()){
        const auto& iter = value.begin();
        result += '"';
        result += iter->first;
        result += '"';
        result += " : ";
        result += boost::apply_visitor(*this, iter->second);

        std::for_each(++value.begin(), value.end(), 
                    [&result](const JSONBasicObject::value_type& val)
                    {result += string_JSON_object(val);}
                    );
    }
    result += "}";
    return std::move(result);
}

std::string JSONBasicValueVisitor::operator()(const JSONBasicArray& array){
    std::string result = "[ ";

    if(!array.empty()){
        result += boost::apply_visitor(*this, array[0]);
        std::for_each(array.begin()+1, array.end(), 
                    [&result](const JSONBasicValue& val)
                    {result += string_JSON_array(val);}
                    );
    }

    result += " ]";
    return std::move(result);
}

std::string string_JSON_object(const JSONBasicObject::value_type& val){
    std::string result(" , ");
    result += '"';
    result += val.first;
    result += '"';
    result += " : ";
    result += boost::apply_visitor(visitor, val.second);
    return std::move(result);
}

std::string string_JSON_array(const JSONBasicValue& val){
    std::string result(" , ");
    result += boost::apply_visitor(visitor, val);
    return std::move(result);
}

// 每一个解析的职责是，读取完后，除了可能有的空白不要改变流，对象和数组可以在最后去掉多余的一个逗号
JSONBasicValue read_JSON_string(std::ifstream& fstream);
JSONBasicValue read_JSON_double(std::ifstream& fstream);
JSONBasicValue read_JSON_bool(std::ifstream& fstream);
JSONBasicValue read_JSON_null(std::ifstream& fstream);
JSONBasicValue read_JSON_object(std::ifstream& fstream);
JSONBasicValue read_JSON_array(std::ifstream& fstream);

std::function<JSONBasicValue(std::ifstream& fstream)> 
judge_next_type(std::ifstream& fstream);

JSONBasicValue resolve_JSON_file(const std::string& filepath){
    // 打开文件
    std::ifstream fstream;
    fstream.open(filepath);

    // 根据字符判断，调用其他函数完成工作
    std::function<JSONBasicValue(std::ifstream& fstream)> func = judge_next_type(fstream);
    JSONBasicValue result = func(fstream);

    // 文件应该就此结束
    int buf;
    while(!fstream.eof()){
        buf = fstream.get();
        if(!isspace(buf) && buf != std::char_traits<char>::eof())   
            throw std::logic_error("JSON syntax error: unexpected ending");   
    }

    return result;  
}

// 解析一个字符串
JSONBasicValue read_JSON_string(std::ifstream& fstream){
    std::string value;
    // 以'"'开头
    char buf;
    fstream>>buf;
    assert(buf == '"');
    // 主体
    while(true){
        buf = fstream.get();
        if(buf == '"' && (value.empty() || value.back() !='\\'))
                break;
        value.push_back(char(buf));      
    }
    return JSONBasicValue(value);
}

JSONBasicValue read_JSON_double(std::ifstream& fstream){
    double value;

    fstream >> value;

    return JSONBasicValue(value);
}

JSONBasicValue read_JSON_bool(std::ifstream& fstream){
    char buf;
    buf = fstream.peek();

    if(buf == 't'){
        std::string value;
        for(size_t i=0; i<4; i++)
            value.push_back(fstream.get());
        assert(value == "true");
        return JSONBasicValue(true);
    }else if (buf == 'f'){
        std::string value;
        for(size_t i=0; i<5; i++)
            value.push_back(fstream.get());
        assert(value == "false");
        return JSONBasicValue(false);
    }else{
        throw std::logic_error("JSON syntax error: unexpected value for type bool");
    }  
}

JSONBasicValue read_JSON_null(std::ifstream& fstream){
    char buf;
    fstream >> buf;
    assert(buf == 'n');

    std::string value;
    for(size_t i=0; i<3; i++)
        value.push_back(fstream.get());
    assert(value == "ull");

    return JSONBasicValue(JSONNullType());
}

JSONBasicValue read_JSON_object(std::ifstream& fstream){
    // 验证头
    char buf;
    fstream >> buf;
    assert(buf == '{');

    JSONBasicObject object;

    while(true){
        // 判断对象描述是否结束
        fstream >> buf;
        if(buf == '}')
            break;
        // 提取名字
        fstream.putback(buf);
        std::string name = boost::get<std::string>(read_JSON_string(fstream));
        // 移除冒号
        fstream >> buf;
        assert(buf == ':');
        // 提取键
        auto func = judge_next_type(fstream);
        JSONBasicValue value = func(fstream);
        object[name] = value;
        // 验证逗号
        fstream >> buf;
        if(buf == '}')
            break;
        assert(buf == ',');
    }
    return object;
}

inline bool JSON_array_empty(std::ifstream& fstream){
    char buf;
    fstream >> buf;
    if(buf == ']')
        return true;
    else{
        fstream.putback(buf);
        return false;
    }
}

JSONBasicValue read_JSON_array(std::ifstream& fstream){
    char buf;
    JSONBasicArray array;
    // 判断头
    fstream >> buf;
    assert(buf == '[');

    // 判断数组非空
    if(JSON_array_empty(fstream))
        return array;

    // 根据数组内容执行
    std::function<JSONBasicValue(std::ifstream& fstream)> func = judge_next_type(fstream);
   
    // 在这里组装数组
    while (true){
        if(JSON_array_empty(fstream))
            return array;

        // 根据数组内容执行
        std::function<JSONBasicValue(std::ifstream& fstream)> func = judge_next_type(fstream);

        array.push_back(func(fstream));

        fstream >> buf;
        if(buf == ']')
            return array;
        else 
            assert(buf == ',');
    }       
        
    return array;
}

std::function<JSONBasicValue(std::ifstream& fstream)> 
judge_next_type(std::ifstream& fstream){
    char buf;
    fstream >> buf;
    fstream.putback(buf);

    switch (buf){
        case '"':
            return read_JSON_string;
        case '{':
            return read_JSON_object;
        case '[':
            return read_JSON_array;
        case '0':case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8':case '9':case '.':case '-':
            return read_JSON_double;
        case 't': case 'f':
            return read_JSON_bool;
        case 'n':
            return read_JSON_null;
        default:
            throw std::logic_error("JSON syntax error: unexpected type");
    }
}

