# learn
记录个人的学习和实践

## ThreadPool
基于C++11的简单线程池实现，单头文件形式

## HTTPProxy
隧道代理服务器端实现

## JSONresolver
JSON解析器。头文件实现分离形式(json.hpp、json.cpp)，单头文件形式(MyJSON.hpp或者my_json_generic.hpp)。
my_json_generic.hpp实现了泛型，需要传递实现了>> getchar putback peek的对象。
该实现尚有几个问题：
1.尚未解决转义字符的问题，但需要改动的地方很少
2.解析速度。在小文件下比python.json快5倍左右，但是大文件比python.json慢1倍。始终比boost::json慢7倍左右。
  瓶颈可能在当前实现的输入流上，为std::string重载流的接口可能解决这个问题
