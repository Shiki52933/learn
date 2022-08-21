#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/array.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <vector>
#include <iostream>
#include <fstream>

namespace global{
unsigned short port = 1000;
unsigned short secretPort = 23333;

void init(){
    // 初始化端口号
    std::string line;
    std::ifstream file;
    file.open("config.txt");
    std::getline(file, line);
    size_t pos = line.find('=')+1;
    port = boost::lexical_cast<unsigned short>(line.substr(pos));
    // 随机初始化收听数据的端口号
    unsigned seed;  
    seed = time(0);
    srand(seed);
    secretPort = rand() % 60000 + 2048;
}

bool check_authentication(const std::string& username, const std::string& pwd){
    std::string line;
    std::ifstream config;
    config.open("config.txt");
    while(!std::getline(config, line).eof()){
        if(line == username+":"+pwd)
            return true;
    }
    return false;
}

}

// AuthConnection负责验证连接
class AuthConnection: public boost::enable_shared_from_this<AuthConnection>
{
public:
  typedef boost::shared_ptr<AuthConnection> pointer;

  static pointer create(boost::asio::io_context& io_context)
  {
    return pointer(new AuthConnection(io_context));
  }

  boost::asio::ip::tcp::socket& socket()
  {
    return m_socket;
  }
  // 被调用的工作方法
  void authenticate()
  {
    m_socket.async_read_some(boost::asio::buffer(m_readBuf), 
                            boost::bind(&AuthConnection::handle_read, shared_from_this(),
                                        boost::asio::placeholders::error,
                                        boost::asio::placeholders::bytes_transferred)
                            );
  }

private:
  AuthConnection(boost::asio::io_context& io_context)
    : m_socket(io_context)
  {
  }

  void handle_read(const boost::system::error_code& e, std::size_t bytes)
  {
    std::cout<<"got connection\n";

    if(e) return;

    // 第一个字节指示长度
    size_t nameLength = boost::lexical_cast<size_t>(m_readBuf[0]);
    std::string username;
    for(size_t i=1; i<nameLength+1; i++)
        username.push_back(m_readBuf[i]);
    // 下面是读取密码
    size_t pwdLength = boost::lexical_cast<size_t>(m_readBuf[nameLength+1]);
    std::string pwd;
    for(size_t i=nameLength+2; i<nameLength+2+pwdLength; i++)
        pwd.push_back(m_readBuf[i]);
    // 验证
    bool ok = global::check_authentication(username, pwd);

    // 发送认证信息
    if(ok){
        std::string ret;
        ret.push_back(char(1));
        ret.append(boost::lexical_cast<std::string>(global::secretPort));
        boost::asio::async_write(m_socket, boost::asio::buffer(ret), 
                                boost::bind(&AuthConnection::handle_write, shared_from_this(),
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred)
                                );
    }else{
        std::string ret;
        ret.push_back(char(0));
        boost::asio::async_write(m_socket, boost::asio::buffer(ret), 
                                boost::bind(&AuthConnection::handle_write, shared_from_this(),
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred)
                                );
    }
  }

  void handle_write(const boost::system::error_code& e, size_t bytes)
  {

  }

  boost::asio::ip::tcp::tcp::socket m_socket;
  boost::array<char, 256> m_readBuf;
};

// 验证服务器类，负责监听和生成验证连接类
class AuthServer{
private:
    boost::asio::io_context& m_io;
    boost::asio::ip::tcp::acceptor m_authServer;
public:
    explicit AuthServer(boost::asio::io_context& io, unsigned short port)
    :m_io(io), m_authServer(io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)){
        start();
    }

    void start(){
        AuthConnection::pointer conn = AuthConnection::create(m_io);
        m_authServer.async_accept(conn->socket(), 
                                  boost::bind(&AuthServer::handle_accept, this, conn, boost::asio::placeholders::error)
                                  );
    }

    void handle_accept(AuthConnection::pointer ptr, const boost::system::error_code& e){
        if(!e){
            // 没有错误，交给AuthConnection处理
            ptr->authenticate();
        }
        start();
    }
};

// 数据连接类，负责单个接入点的数据处理
class DataConnection: public boost::enable_shared_from_this<DataConnection>{
private:
    std::string method;
    std::string http;
    std::string firstHeader;
    boost::asio::io_context& m_io;
    boost::asio::ip::tcp::socket m_socket;
    boost::asio::ip::tcp::socket m_dataSocket; // 该套接字对外联络
    boost::asio::ip::tcp::resolver m_resolver;
    boost::array<char, 2048> m_buffer;
    boost::array<char, 2048> m_serverBuffer;

    explicit DataConnection(boost::asio::io_context& io):m_io(io), m_socket(io), m_dataSocket(io), m_resolver(io) {}

public:
    typedef boost::shared_ptr<DataConnection> pointer;

    boost::asio::ip::tcp::socket& socket() {return m_socket;}
    // 对外的创建接口
    static pointer create(boost::asio::io_context& io)
    {
        return pointer(new DataConnection(io));
    }
    // 这个函数执行路由服务
    void route(){
        m_socket.async_read_some(boost::asio::buffer(m_buffer), 
                                boost::bind(&DataConnection::handle_connect, 
                                            shared_from_this(),
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred)
                                );
        // 调试信息
        std::cout<<"route succeeded\n";
    }
    // 这个函数处理第一次的连接
    void handle_connect(const boost::system::error_code& e, size_t bytes){
        if(e){
            std::cout<<e.message()<<'\n';
            std::cout<<"error met\n";
            return;
        }
        // 调试信息
        std::cout.write(m_buffer.data(), bytes);
        
        // 获取连接信息
        for(size_t i=0; i<bytes; i++)
            firstHeader.push_back(m_buffer[i]);
        // 切割字符串，提取\r\n
        std::vector<std::string> lines;
        boost::split_regex(lines, firstHeader, boost::regex("\r\n"));
        // 分解第一行
        std::vector<std::string> firstLineParts;
        boost::split(firstLineParts, lines[0], boost::is_any_of(" "), boost::token_compress_on);
        method = firstLineParts[0];
        http = firstLineParts[2];

        // 提取host:
        std::string host, port;
        for(const std::string& line: lines){
            if(line.find("Host: ")==0){
                std::vector<std::string> resolve;
                boost::split(resolve, line, boost::is_any_of(": /"), boost::token_compress_on);
                host = resolve.at(1);
                // 下面尝试转换可能存在的端口号
                try{
                    boost::lexical_cast<unsigned short>(resolve.back());
                    port = resolve.back();
                }catch(const std::exception& e){

                }

                break;
            }
        }
        if(host.empty())
            handle_host_empty(http);
        // 提取port
        std::vector<std::string> resolve;
        boost::split(resolve, firstLineParts[1], boost::is_any_of(":/"), boost::token_compress_on);
        try{
            boost::lexical_cast<unsigned short>(resolve.back());
            port = resolve.back();
        }catch(std::exception& e){

        }
        
        // 调试信息
        std::cout<<host<<'\t'<<port<<'\n';

        // 默认端口号为80
        if(port.empty())
            port = "80";

        // 转换地址
        m_resolver.async_resolve(host, port, 
                               boost::bind(&DataConnection::handle_after_resolve, 
                                            shared_from_this(),
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::results)
                                );
    }
    // 这个函数处理域名为空的情况
    void handle_host_empty(std::string s)
    {
        std::string response(s);
        response += " 400 Bad Request\r\n";

        boost::asio::async_write(m_socket, boost::asio::buffer(response),
                                boost::bind(&DataConnection::void_write, 
                                            shared_from_this(),
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred)
                                );
    }
    // 解析完域名后的回调函数
    void handle_after_resolve(const boost::system::error_code& e, boost::asio::ip::tcp::resolver::results_type results)
    {
        if(e){
            std::cout<<"resolve error\n";
            std::cout<<e.message()<<'\n';
            return;
        }

        m_dataSocket.async_connect(*results.begin(), 
                                    boost::bind(&DataConnection::handle_after_connect, 
                                                shared_from_this(),
                                                boost::asio::placeholders::error)
                                    );
    }
    // 连接上服务器后的回调函数
    void handle_after_connect(const boost::system::error_code& e)
    {
        if(e){
            std::cout<<"error met!"<<'\n';
            std::cout<<e.message()<<'\n';
            return;
        }

        if(method != "CONNECT"){
            // 如果不是CONNECT方法，直接转发，不要向客户端发送回执
            boost::asio::async_write(m_dataSocket, 
                                    boost::asio::buffer(firstHeader),
                                    boost::bind(&DataConnection::transfer_data, 
                                                shared_from_this(),
                                                boost::asio::placeholders::error,
                                                boost::asio::placeholders::bytes_transferred)
                                    );
        }
        else{
            // CONNECT方法，向客户端发送连接成功
            std::string response(http);
            response += " 200 OK\r\n\r\n";
            boost::asio::async_write(m_socket, 
                                    boost::asio::buffer(response), 
                                    boost::bind(&DataConnection::transfer_data, 
                                                shared_from_this(),
                                                boost::asio::placeholders::error,
                                                boost::asio::placeholders::bytes_transferred)
                                    );
        }
    }
    // 负责数据转发，调用两个单方向的转发函数
    void transfer_data(const boost::system::error_code& e, size_t bytes) 
    {
        if(e){
            std::cout<<"error met!"<<'\n';
            std::cout<<e.message()<<'\n';
            return;
        }
        // 调试信息
        std::cout<<"开始数据转发\n";

        client2server();
        server2client();
    }
    // 客户端到服务器的读取函数
    void client2server()
    {
        m_socket.async_read_some(boost::asio::buffer(m_buffer),
                                boost::bind(&DataConnection::handle_client2server, 
                                            shared_from_this(),
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred)
                                );
    }
    // 客户端到服务器的转发函数
    void handle_client2server(const boost::system::error_code& e, size_t bytes)
    {
        if(e && e != boost::asio::error::misc_errors::eof){
            // 其他类型的错误直接返回
            std::cout<<"error met!"<<'\n';
            std::cout<<e.message()<<'\n';
            boost::asio::async_write(m_dataSocket,
                                    boost::asio::buffer(m_buffer, bytes),
                                    boost::bind(&DataConnection::void_write, 
                                                shared_from_this(),
                                                boost::asio::placeholders::error,
                                                boost::asio::placeholders::bytes_transferred)
                                    );
            return;
        }
        // 正常情况
        auto ptr = shared_from_this();
        boost::asio::async_write(m_dataSocket,
                                boost::asio::buffer(m_buffer, bytes),
                                [ptr](const boost::system::error_code& e, size_t bytes)
                                {ptr->client2server();}
                                );
        
    }
    // 服务端到客户端的读取函数
    void server2client()
    {
        m_dataSocket.async_read_some(boost::asio::buffer(m_serverBuffer),
                                    boost::bind(&DataConnection::handle_server2client, 
                                                shared_from_this(),
                                                boost::asio::placeholders::error,
                                                boost::asio::placeholders::bytes_transferred)
                                    );
    }
    // 服务端到客户端的转发函数
    void handle_server2client(const boost::system::error_code& e, size_t bytes)
    {
        if(e && e != boost::asio::error::misc_errors::eof){
            std::cout<<"error met!"<<'\n';
            std::cout<<e.message()<<'\n';
            boost::asio::async_write(m_socket,
                                    boost::asio::buffer(m_serverBuffer, bytes),
                                    boost::bind(&DataConnection::void_write, 
                                                shared_from_this(),
                                                boost::asio::placeholders::error,
                                                boost::asio::placeholders::bytes_transferred)
                                );
            return;
        }
        auto ptr = shared_from_this();
        boost::asio::async_write(m_socket,
                                boost::asio::buffer(m_serverBuffer, bytes),
                                [ptr](const boost::system::error_code& e, size_t bytes)
                                {ptr->server2client();}
                                );
        
    }

    void void_write(const boost::system::error_code& e, size_t bytes)  {}

    ~DataConnection()
    {
        try{
            m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
            m_socket.close();
            m_dataSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
            m_dataSocket.close();
        }catch(std::exception& e){
            
        }
    }
};

class DataServer{
private:
    boost::asio::io_context& m_io;
    boost::asio::ip::tcp::acceptor m_dataServer;
public:
    explicit DataServer(boost::asio::io_context& io, unsigned short port)
    :m_io(io), m_dataServer(io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)){
        start();
    }

    void start(){
        DataConnection::pointer conn = DataConnection::create(m_io);

        m_dataServer.async_accept(conn->socket(), 
                                  boost::bind(&DataServer::handle_accept, 
                                            this, conn, 
                                            boost::asio::placeholders::error)
                                  );
    }

    void handle_accept(DataConnection::pointer ptr, const boost::system::error_code& e){
        if(!e){
            // 没有错误，交给AuthConnection处理
            ptr->route();
        }
        start();
    }
};

int main(){
    try{
        global::init();
        std::cout<<global::secretPort<<'\n';
    
        boost::asio::io_context io;
        AuthServer authServer(io, global::port);
        DataServer dataServer(io, global::secretPort);
        std::cout<<global::secretPort<<'\n';
        io.run();
    }catch(const std::exception& e){
        std::cerr<<e.what()<<'\n';
    }
}