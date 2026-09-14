//   This file is part of moonpotato/zstring_view
//   zstring_view: A non-owning reference to a null-terminated part of a string
//
//   Copyright 2018 moonpotato <git@funnelweb.xyz>
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.

#ifndef ZSTRING_VIEW_INCLUDED
#define ZSTRING_VIEW_INCLUDED

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

#include "opthelpers.h"

namespace al {

    template<typename CharT, typename Traits = std::char_traits<CharT>>
    class basic_zstring_view;

    using zstring_view = basic_zstring_view<char>;
    using wzstring_view = basic_zstring_view<wchar_t>;
    using u8zstring_view = basic_zstring_view<char8_t>;
    using u16zstring_view = basic_zstring_view<char16_t>;
    using u32zstring_view = basic_zstring_view<char32_t>;

    template<typename CharT, typename Traits>
    class basic_zstring_view {
    public:
        using underlying_type        = std::basic_string_view<CharT, Traits>;

        using traits_type            = typename underlying_type::traits_type;
        using value_type             = typename underlying_type::value_type;
        using pointer                = typename underlying_type::pointer;
        using const_pointer          = typename underlying_type::const_pointer;
        using reference              = typename underlying_type::reference;
        using const_reference        = typename underlying_type::const_reference;
        using const_iterator         = typename underlying_type::const_iterator;
        using iterator               = typename underlying_type::iterator;
        using const_reverse_iterator = typename underlying_type::const_reverse_iterator;
        using reverse_iterator       = typename underlying_type::reverse_iterator;
        using size_type              = typename underlying_type::size_type;
        using difference_type        = typename underlying_type::difference_type;

        static constexpr auto npos = underlying_type::npos;

        constexpr basic_zstring_view() noexcept = default;
        constexpr basic_zstring_view(basic_zstring_view const &other) noexcept = default;
        explicit(false) constexpr basic_zstring_view(CharT const *s LIFETIMEBOUND) : m_view{s} { }
        template<typename Alloc> explicit(false) constexpr
        basic_zstring_view(std::basic_string<CharT, Traits, Alloc> const &s LIFETIMEBOUND)
            : m_view{s}
        { }

        // Needed as a workaround for gcc bug #61648
        // Allows non-friend string literal operators the ability to indirectly call the private constructor
        // Can't use the CharT const* constructor in case the string contains null characters
        // Safe calling of this requires *certainty* that s[count] is a null character
        // Has to be public thusly, but don't call this
        [[nodiscard]] static constexpr
        auto INTERNAL_unsafe_make_from_string_range(CharT const *s LIFETIMEBOUND, size_type count)
            -> basic_zstring_view
        {
            return basic_zstring_view{s, count};
        }

        constexpr
        auto operator=(const basic_zstring_view&) & noexcept LIFETIMEBOUND -> basic_zstring_view&
            = default;
        template<typename Alloc>
        constexpr auto operator=(std::basic_string<CharT, Traits, Alloc> const &rhs) & noexcept
            LIFETIMEBOUND -> basic_zstring_view&
        { m_view = rhs; return *this; }

        [[nodiscard]] constexpr const_iterator begin() const noexcept
        {
            return m_view.begin();
        }

        [[nodiscard]] constexpr const_iterator cbegin() const noexcept
        {
            return m_view.cbegin();
        }

        [[nodiscard]] constexpr const_iterator end() const noexcept
        {
            return m_view.end();
        }

        [[nodiscard]] constexpr const_iterator cend() const noexcept
        {
            return m_view.cend();
        }

        [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept
        {
            return m_view.rbegin();
        }

        [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept
        {
            return m_view.crbegin();
        }

        [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept
        {
            return m_view.rend();
        }

        [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept
        {
            return m_view.crend();
        }

        [[nodiscard]] constexpr const_reference operator[](size_type pos) const
        {
            return m_view[pos];
        }

        [[nodiscard]] constexpr const_reference at(size_type pos) const
        {
            return m_view.at(pos);
        }

        [[nodiscard]] constexpr const_reference front() const
        {
            return m_view.front();
        }

        [[nodiscard]] constexpr const_reference back() const
        {
            return m_view.back();
        }

        [[nodiscard]] constexpr const_pointer data() const noexcept
        {
            return m_view.data();
        }

        // Additional function
        [[nodiscard]] constexpr const_pointer c_str() const noexcept
        {
            return m_view.data();
        }

        [[nodiscard]] constexpr size_type size() const noexcept
        {
            return m_view.size();
        }

        [[nodiscard]] constexpr size_type length() const noexcept
        {
            return m_view.length();
        }

        [[nodiscard]] constexpr size_type max_size() const noexcept
        {
            return m_view.max_size();
        }

        [[nodiscard]] constexpr bool empty() const noexcept
        {
            return m_view.empty();
        }

        constexpr void remove_prefix(size_type n)
        {
            m_view.remove_prefix(n);
        }

        constexpr void swap(basic_zstring_view& v) noexcept
        {
            swap(m_view, v.m_view);
        }

        size_type copy(CharT* dest, size_type count, size_type pos = 0) const
        {
            return m_view.copy(dest, count, pos);
        }

        [[nodiscard]] constexpr
        auto substr(size_type pos = 0, size_type count = npos) const -> underlying_type
        {
            return m_view.substr(pos, count);
        }

        // Additional function
        [[nodiscard]] constexpr basic_zstring_view suffix(size_type start = 0) const
        {
            return basic_zstring_view{m_view.substr(start, npos)};
        }

        [[nodiscard]] constexpr int compare(underlying_type v) const noexcept
        {
            return m_view.compare(v);
        }

        [[nodiscard]] constexpr
        int compare(size_type pos1, size_type count1, underlying_type v) const
        {
            return m_view.compare(pos1, count1, v);
        }

        [[nodiscard]] constexpr
        int compare(size_type pos1, size_type count1, underlying_type v,
            size_type pos2, size_type count2) const
        {
            return m_view.compare(pos1, count1, v, pos2, count2);
        }

        [[nodiscard]] constexpr int compare(const CharT* s) const
        {
            return m_view.compare(s);
        }

        [[nodiscard]] constexpr
        int compare(size_type pos1, size_type count1,
            const CharT* s, size_type count2) const
        {
            return m_view.compare(pos1, count1, s, count2);
        }

        [[nodiscard]] constexpr bool starts_with(underlying_type x) const noexcept
        {
            return m_view.starts_with(x);
        }

        [[nodiscard]] constexpr bool starts_with(CharT x) const noexcept
        {
            return m_view.starts_with(x);
        }

        [[nodiscard]] constexpr bool starts_with(const CharT* x) const
        {
            return m_view.starts_with(x);
        }

        [[nodiscard]] constexpr bool ends_with(underlying_type x) const noexcept
        {
            return m_view.ends_with(x);
        }

        [[nodiscard]] constexpr bool ends_with(CharT x) const noexcept
        {
            return m_view.ends_with(x);
        }

        [[nodiscard]] constexpr bool ends_with(const CharT* x) const
        {
            return m_view.ends_with(x);
        }

        [[nodiscard]] constexpr size_type find(underlying_type v, size_type pos = 0) const noexcept
        {
            return m_view.find(v, pos);
        }

        [[nodiscard]] constexpr size_type find(CharT ch, size_type pos = 0) const noexcept
        {
            return m_view.find(ch, pos);
        }

        [[nodiscard]] constexpr size_type find(const CharT* s, size_type pos, size_type count) const
        {
            return m_view.find(s, pos, count);
        }

        [[nodiscard]] constexpr size_type find(const CharT* s, size_type pos = 0) const
        {
            return m_view.find(s, pos);
        }

        [[nodiscard]] constexpr size_type rfind(underlying_type v, size_type pos = npos) const noexcept
        {
            return m_view.rfind(v, pos);
        }

        [[nodiscard]] constexpr size_type rfind(CharT ch, size_type pos = npos) const noexcept
        {
            return m_view.rfind(ch, pos);
        }

        [[nodiscard]] constexpr size_type rfind(const CharT* s, size_type pos, size_type count) const
        {
            return m_view.rfind(s, pos, count);
        }

        [[nodiscard]] constexpr size_type rfind(const CharT* s, size_type pos = npos) const
        {
            return m_view.rfind(s, pos);
        }

        [[nodiscard]] constexpr size_type find_first_of(underlying_type v, size_type pos = 0) const noexcept
        {
            return m_view.find_first_of(v, pos);
        }

        [[nodiscard]] constexpr size_type find_first_of(CharT c, size_type pos = 0) const noexcept
        {
            return m_view.find_first_of(c, pos);
        }

        [[nodiscard]] constexpr size_type find_first_of(const CharT* s, size_type pos, size_type count) const
        {
            return m_view.find_first_of(s, pos, count);
        }

        [[nodiscard]] constexpr size_type find_first_of(const CharT* s, size_type pos = 0) const
        {
            return m_view.find_first_of(s, pos);
        }

        [[nodiscard]] constexpr size_type find_last_of(underlying_type v, size_type pos = npos) const noexcept
        {
            return m_view.find_last_of(v, pos);
        }

        [[nodiscard]] constexpr size_type find_last_of(CharT c, size_type pos = npos) const noexcept
        {
            return m_view.find_last_of(c, pos);
        }

        [[nodiscard]] constexpr size_type find_last_of(const CharT* s, size_type pos, size_type count) const
        {
            return m_view.find_last_of(s, pos, count);
        }

        [[nodiscard]] constexpr size_type find_last_of(const CharT* s, size_type pos = npos) const
        {
            return m_view.find_last_of(s, pos);
        }

        [[nodiscard]] constexpr size_type find_first_not_of(underlying_type v, size_type pos = 0) const noexcept
        {
            return m_view.find_first_not_of(v, pos);
        }

        [[nodiscard]] constexpr size_type find_first_not_of(CharT c, size_type pos = 0) const noexcept
        {
            return m_view.find_first_not_of(c, pos);
        }

        [[nodiscard]] constexpr size_type find_first_not_of(const CharT* s, size_type pos, size_type count) const
        {
            return m_view.find_first_not_of(s, pos, count);
        }

        [[nodiscard]] constexpr size_type find_first_not_of(const CharT* s, size_type pos = 0) const
        {
            return m_view.find_first_not_of(s, pos);
        }

        [[nodiscard]] constexpr size_type find_last_not_of(underlying_type v, size_type pos = npos) const noexcept
        {
            return m_view.find_last_not_of(v, pos);
        }

        [[nodiscard]] constexpr size_type find_last_not_of(CharT c, size_type pos = npos) const noexcept
        {
            return m_view.find_last_not_of(c, pos);
        }

        [[nodiscard]] constexpr size_type find_last_not_of(const CharT* s, size_type pos, size_type count) const
        {
            return m_view.find_last_not_of(s, pos, count);
        }

        [[nodiscard]] constexpr size_type find_last_not_of(const CharT* s, size_type pos = npos) const
        {
            return m_view.find_last_not_of(s, pos);
        }

        [[nodiscard]] constexpr underlying_type view() const noexcept
        {
            return m_view;
        }

        [[nodiscard]] constexpr operator underlying_type() const noexcept
        {
            return m_view;
        }


        [[nodiscard]] friend constexpr
        auto operator<=>(basic_zstring_view const lhs, basic_zstring_view const rhs) noexcept
        { return lhs.view() <=> rhs.view(); }
        [[nodiscard]] friend constexpr
        auto operator<=>(basic_zstring_view const lhs, underlying_type const rhs) noexcept
        { return lhs.view() <=> rhs; }
        template<typename Alloc> [[nodiscard]] friend constexpr
        auto operator<=>(basic_zstring_view const lhs,
            std::basic_string<CharT, Traits, Alloc> const &rhs) noexcept
        { return lhs.view() <=> rhs; }
        [[nodiscard]] friend constexpr
        auto operator<=>(basic_zstring_view const lhs, CharT const *rhs) noexcept
        { return lhs.view() <=> rhs; }

        [[nodiscard]] friend constexpr
        auto operator==(basic_zstring_view const lhs, basic_zstring_view const rhs) noexcept
            -> bool
        { return lhs.view() == rhs.view(); }
        [[nodiscard]] friend constexpr
        auto operator==(basic_zstring_view const lhs, underlying_type const rhs) noexcept -> bool
        { return lhs.view() == rhs; }
        template<typename Alloc> [[nodiscard]] friend constexpr
        auto operator==(basic_zstring_view const lhs,
            std::basic_string<CharT, Traits, Alloc> const &rhs) noexcept -> bool
        { return lhs.view() == rhs; }
        [[nodiscard]] friend constexpr
        auto operator==(basic_zstring_view const lhs, CharT const *rhs) noexcept -> bool
        { return lhs.view() == rhs; }

    private:
        // Private constructor, called by the string literal operators
        explicit constexpr
        basic_zstring_view(CharT const *const s LIFETIMEBOUND, size_type const count)
            : m_view{s, count}
        { }

        // Private constructor, needed by suffix()
        explicit constexpr basic_zstring_view(underlying_type const v) noexcept : m_view{v} { }

        underlying_type m_view{""};
    };


    inline namespace literals {
        inline namespace zstring_view_literals {
            consteval
            auto operator ""_zsv(char const*const str, std::size_t const len) noexcept
                -> zstring_view
            { return zstring_view::INTERNAL_unsafe_make_from_string_range(str, len); }

            consteval
            auto operator ""_zsv(char8_t const*const str, std::size_t const len) noexcept
                -> u8zstring_view
            { return u8zstring_view::INTERNAL_unsafe_make_from_string_range(str, len); }

            consteval
            auto operator ""_zsv(char16_t const*const str, std::size_t const len) noexcept
                -> u16zstring_view
            { return u16zstring_view::INTERNAL_unsafe_make_from_string_range(str, len); }

            consteval
            auto operator ""_zsv(char32_t const*const str, std::size_t const len) noexcept
                -> u32zstring_view
            { return u32zstring_view::INTERNAL_unsafe_make_from_string_range(str, len); }

            consteval
            auto operator ""_zsv(wchar_t const*const str, std::size_t const len) noexcept
                -> wzstring_view
            { return wzstring_view::INTERNAL_unsafe_make_from_string_range(str, len); }
        }
    }
}

#endif
