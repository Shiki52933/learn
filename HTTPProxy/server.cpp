#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/array.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/asio.hpp>
#include <boost/asio/query.hpp>
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

bool checkAuthentication(const std::string& username, const std::string& pwd){
    std::string line;
    std::ifstream config;
    config.open("config.txt");
    while(!std::getline(config, line).eof()){
        // 调试信息
        //std::cout<<username+":"+pwd<<'\n';

        if(line == username+":"+pwd)
            return true;
    }
    return false;
}

std::string decode(const std::string& s){
    return s;
}

std::string encode(const std::string& s){
    return s;
}

}

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
    // 解密并验证
    username = global::decode(username);
    pwd = global::decode(pwd);
    bool ok = global::checkAuthentication(username, pwd);
    // 调试信息，后删
    //std::cout<<username<<'\t'<<pwd<<'\n';

    // 发送认证信息
    if(ok){
        std::string ret;
        ret.push_back(char(1));
        ret.append(boost::lexical_cast<std::string>(global::secretPort));
        ret = global::encode(ret);
        boost::asio::async_write(m_socket, boost::asio::buffer(ret), 
                                boost::bind(&AuthConnection::handle_write, shared_from_this(),
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred)
                                );
    }else{
        std::string ret;
        ret.push_back(char(0));
        ret = global::encode(ret);
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
                                  boost::bind(&AuthServer::handleAccept, this, conn, boost::asio::placeholders::error)
                                  );
    }

    void handleAccept(AuthConnection::pointer ptr, const boost::system::error_code& e){
        if(!e){
            // 没有错误，交给AuthConnection处理
            ptr->authenticate();
        }
        start();
    }
};

class DataConnection: public boost::enable_shared_from_this<DataConnection>{
private:
    std::string http;
    boost::asio::io_context& m_io;
    boost::asio::ip::tcp::socket m_socket;
    boost::asio::ip::tcp::socket m_dataSocket; // 该套接字对外联络
    boost::asio::ip::tcp::resolver m_resolver;
    boost::array<char, 2048> m_buffer;
    boost::array<char, 2048> m_outerBuffer;

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
        // 调试信息
        // std::cout<<"route start\n";

        m_socket.async_read_some(boost::asio::buffer(m_buffer), 
                                boost::bind(&DataConnection::handleConnect, 
                                            shared_from_this(),
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred)
                                );
        // 调试信息
        std::cout<<"route succeeded\n";
    }
    // 这个函数处理第一次的连接
    void handleConnect(const boost::system::error_code& e, size_t bytes){
        // 调试信息
        // std::cout<<"connect start\n";

        if(e){
            // 调试信息
            std::cout<<"error met\n";
            return;
        }
        // 调试信息
        std::cout.write(m_buffer.data(), bytes);
        

        // 获取连接信息
        std::string connInfo;
        for(size_t i=0; i<bytes; i++)
            connInfo.push_back(m_buffer[i]);
        // 切割字符串，提取\r\n
        std::vector<std::string> res;
        boost::split_regex(res, connInfo, boost::regex("\r\n"));

        // 调试信息
        // std::cout<<res.size()<<'\n';

        std::vector<std::string> firstLine;
        boost::split(firstLine, res.at(0), boost::is_any_of(" "), boost::token_compress_on);
        http = firstLine.at(2);

        // 调试信息
        // std::cout<<firstLine[2]<<'\n';

        // 提取host:
        std::string host;
        for(const std::string& arg: res){
            if(arg.find("Host: ")==0){
                std::vector<std::string> resolve;
                boost::split(resolve, arg, boost::is_any_of(": /"), boost::token_compress_on);
                host = resolve.at(1);
                break;
            }
        }
        // 调试信息
        // std::cout<<host<<'\n';

        if(host.empty())
            handleHostEmpty(http);
        // 提取port
        std::vector<std::string> resolve;
        boost::split(resolve, firstLine[1], boost::is_any_of(":/"), boost::token_compress_on);
        std::string port = resolve.back();

        // 调试信息
        // std::cout<<port<<'\n'; 

        // 调试信息
        std::cout<<host<<'\t'<<port<<'\n';

        // 转换地址
        m_resolver.async_resolve(host, port, 
                               boost::bind(&DataConnection::handleAfterResolve, 
                                            shared_from_this(),
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::results)
                                );
    }

    void handleHostEmpty(std::string s)
    {
        std::string response(s);
        response += " 400 Bad Request\r\n";
        // 调试信息
        std::cout<<response<<'\n';

        boost::asio::async_write(m_socket, boost::asio::buffer(response),
                                boost::bind(&DataConnection::voidWrite, 
                                            shared_from_this(),
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred)
                                );
    }

    void handleAfterResolve(const boost::system::error_code& e, boost::asio::ip::tcp::resolver::results_type results)
    {
        if(e){
            std::cout<<"resolve error\n";
            std::cout<<e.message()<<'\n';
            return;
        }
        m_dataSocket.async_connect(*results.begin(), 
                                    boost::bind(&DataConnection::handleAfterConnect, 
                                                shared_from_this(),
                                                boost::asio::placeholders::error)
                                    );
    }

    void handleAfterConnect(const boost::system::error_code& e)
    {
        // 向客户端发送连接成功
        std::string response(http);
        response += " 200 OK\r\n\r\n";
        boost::asio::async_write(m_socket, 
                                boost::asio::buffer(response), 
                                boost::bind(&DataConnection::transferData, 
                                            shared_from_this(),
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred)
                                );
    }

    void transferData(const boost::system::error_code& e, size_t bytes) 
    {
        // 调试信息
        std::cout<<"开始数据转发\n";

        client2outer();
        outer2client();
    }

    void client2outer()
    {
        m_socket.async_read_some(boost::asio::buffer(m_buffer),
                                boost::bind(&DataConnection::handleClient2outer, 
                                            shared_from_this(),
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred)
                                );
    }

    void handleClient2outer(const boost::system::error_code& e, size_t bytes)
    {
        auto ptr = shared_from_this();
        boost::asio::async_write(m_dataSocket,
                                boost::asio::buffer(m_buffer, bytes),
                                [ptr](const boost::system::error_code& e, size_t bytes)
                                {ptr->client2outer();}
                                );
    }

    void outer2client()
    {
        m_dataSocket.async_read_some(boost::asio::buffer(m_outerBuffer),
                                    boost::bind(&DataConnection::handleOuter2Client, 
                                                shared_from_this(),
                                                boost::asio::placeholders::error,
                                                boost::asio::placeholders::bytes_transferred)
                                    );
    }

    void handleOuter2Client(const boost::system::error_code& e, size_t bytes)
    {
        auto ptr = shared_from_this();
        boost::asio::async_write(m_socket,
                                boost::asio::buffer(m_outerBuffer, bytes),
                                [ptr](const boost::system::error_code& e, size_t bytes)
                                {ptr->outer2client();}
                                );
    }

    void voidWrite(const boost::system::error_code& e, size_t bytes)  {}
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
        // 调试信息
        // std::cout<<"conn 创建成功\n";

        m_dataServer.async_accept(conn->socket(), 
                                  boost::bind(&DataServer::handleAccept, 
                                            this, conn, 
                                            boost::asio::placeholders::error)
                                  );
    }

    void handleAccept(DataConnection::pointer ptr, const boost::system::error_code& e){
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
        global::secretPort=36951;
        std::cout<<global::secretPort<<'\n';
    
        boost::asio::io_context io;
        AuthServer authServer(io, global::port);
        DataServer dataServer(io, global::secretPort);
        io.run();
    }catch(const std::exception& e){
        std::cerr<<e.what()<<'\n';
    }
}