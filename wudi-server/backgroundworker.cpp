#include "backgroundworker.hpp"
/*
namespace farasha
{
    BackgroundWorker::BackgroundWorker( std::string const & address, std::vector<QString>&& numbers,
                                        safe_proxy& proxy_provider, QObject* parent ):
        QObject{ parent }, numbers_{ std::move( numbers ) }, proxy_provider_{ proxy_provider },
        address_{ address }
    {
    }

    void BackgroundWorker::fetch_result( int const socket_count )
    {

    }

    void BackgroundWorker::run()
    {
        auto& context{ get_network_context() };
        std::list<std::unique_ptr<http_socket>> sockets{};
        for( int i = 0; i != utilities::MAX_OPEN_SOCKET; ++i ){
            sockets.push_back( std::make_unique<http_socket>( context, proxy_provider_, numbers_,
                                                              address_ ) );
            QObject::connect( sockets.back().get(), &http_socket::result_available,
                              [=]( SearchResultType t, QString const & number )
            {
                emit result_available( t, number );
            });
            sockets.back()->start_connect();
        }
        context.run();
    }
}
*/