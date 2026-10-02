#ifndef INSTANCE_OF_HPP
#define INSTANCE_OF_HPP

namespace al {

template<typename T, template<typename...> typename U>
inline auto constexpr is_instance_of_v = false;

template<template<typename...> typename U, typename... Vs>
inline auto constexpr is_instance_of_v<U<Vs...>, U> = true;


template<typename T, template<typename...> typename U>
concept instance_of = is_instance_of_v<T, U>;

}

#endif /* INSTANCE_OF_HPP */
