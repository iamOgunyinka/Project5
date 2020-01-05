#include "safe_proxy.hpp"
/*
namespace wudi_server
{
    safe_proxy::safe_proxy( MainDialog* parent, QNetworkAccessManager& network ):
        parent_dialog{ parent }, network_manager_{ network }
    {
    }

    std::optional<safe_proxy::endpoint_ptr> safe_proxy::next_endpoint()
    {
        {
            std::lock_guard<std::mutex> lock_g{ mutex_ };
            if( has_error || endpoints_.empty() ) return std::nullopt;
            if( count_ >= endpoints_.size() ){
                count_ = 0;
                while( count_ < endpoints_.size() ){
                    if( endpoints_[count_]->property == ProxyProperty::ProxyActive ){
                        return endpoints_[count_];
                    }
                    count_++;
                }
            } else {
                return endpoints_[count_++];
            }
        }
        GetMoreProxies();
        return next_endpoint();
    }

    void safe_proxy::clear()
    {
        std::lock_guard<std::mutex> lock_g{ mutex_ };
        endpoints_.clear();
    }

    void safe_proxy::push_back(custom_endpoint ep)
    {
        std::lock_guard<std::mutex> lock_g{ mutex_ };
        endpoints_.emplace_back( std::make_shared<custom_endpoint>( std::move( ep ) ) );
    }

    void custom_endpoint::swap( custom_endpoint & other )
    {
        std::swap( other.endpoint, this->endpoint );
        std::swap( other.property, this->property );
    }

    void swap( custom_endpoint &a, custom_endpoint & b )
    {
        a.swap( b );
    }
}
*/