#pragma once

template <typename CT, typename ... A> struct function
: public function<decltype(&CT::operator())(A...)> {};

template <typename C> struct function<C> {
private:
    C mObject;

public:
    function(const C & obj)
        : mObject(obj) {}

    template<typename... Args> typename
    std::result_of<C(Args...)>::type operator()(Args... a) {
        return this->mObject.operator()(a...);
    }

    template<typename... Args> typename
    std::result_of<const C(Args...)>::type operator()(Args... a) const {
        return this->mObject.operator()(a...);
    }
};

namespace make {
    template<typename C> auto function(const C & obj) {
        return ::function<C>(obj);
    }
}
