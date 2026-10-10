#ifndef AL_SPANSTREAM_HPP
#define AL_SPANSTREAM_HPP

#include <concepts>
#include <istream>
#include <span>
#include <streambuf>

#include "alnumeric.h"
#include "opthelpers.h"


namespace al {

template<typename CharT, typename Traits = std::char_traits<CharT>>
class basic_spanbuf final : public std::basic_streambuf<CharT, Traits> {
    std::span<CharT> mData;
    std::ios_base::openmode mMode;

    using streambuf_type = std::basic_streambuf<CharT, Traits>;

public:
    using char_type   = CharT;
    using int_type    = typename Traits::int_type;
    using pos_type    = typename Traits::pos_type;
    using off_type    = typename Traits::off_type;
    using traits_type = Traits;

    static_assert(std::same_as<char_type, typename traits_type::char_type>);

    basic_spanbuf() : basic_spanbuf{std::ios_base::in | std::ios_base::out} { }
    explicit
    basic_spanbuf(std::ios_base::openmode const which) : basic_spanbuf{std::span<CharT>{}, which}
    { }
    explicit
    basic_spanbuf(std::span<CharT> const data,
        std::ios_base::openmode const which = std::ios_base::in|std::ios_base::out)
        : mData{data}, mMode{which}
    {
        constexpr auto modebits = std::ios_base::in | std::ios_base::out | std::ios_base::binary
            | std::ios_base::app | std::ios_base::trunc | std::ios_base::ate;
        if((which&~modebits) != 0)
            throw std::ios_base::failure("Invalid open mode bits");
        if((which&std::ios_base::in) != 0)
            this->setg(data.data(), data.data(), std::to_address(data.end()));
        if((which&std::ios_base::out) != 0)
        {
            this->setp(data.data(), std::to_address(data.end()));
            if((which&std::ios_base::ate))
                this->pbump(al::saturate_cast<int>(data.size()));
        }
    }
    basic_spanbuf(basic_spanbuf const&) = delete;
    basic_spanbuf(basic_spanbuf&& rhs) noexcept : basic_spanbuf{} { swap(rhs); }

    auto operator=(basic_spanbuf const&) & -> basic_spanbuf& = delete;
    auto operator=(basic_spanbuf&& rhs) & noexcept LIFETIMEBOUND -> basic_spanbuf&
    {
        if(this != &rhs)
            basic_spanbuf{std::move(rhs)}.swap(*this);
        return *this;
    }

    auto swap(basic_spanbuf& rhs) noexcept -> void
    {
        streambuf_type::swap(rhs);
        std::swap(mData, rhs.mData);
        std::swap(mMode, rhs.mMode);
    }

    [[nodiscard]]
    auto span() const noexcept -> std::span<CharT>
    {
        if((mMode&std::ios_base::out) != 0)
            return std::span{this->pbase(), this->pptr()};
        return mData;
    }

    auto span(std::span<CharT> const data) noexcept -> void
    {
        mData = data;
        if((mMode&std::ios_base::in) != 0)
            this->setg(data.data(), data.data(), std::to_address(data.end()));
        if((mMode&std::ios_base::out) != 0)
        {
            this->setp(data.data(), std::to_address(data.end()));
            if((mMode&std::ios_base::ate) != 0)
                this->pbump(al::saturate_cast<int>(data.size()));
        }
    }

protected:
    auto seekoff(off_type const offset, std::ios_base::seekdir const whence,
        std::ios_base::openmode const mode) -> pos_type final
    {
        constexpr auto modebits = std::ios_base::in | std::ios_base::out;
        if(mode == 0 or (mode&~modebits) != 0)
            return traits_type::eof();

        if(((mode&std::ios_base::out) and not (mMode&std::ios_base::out))
            or ((mode&std::ios_base::in) and not (mMode&std::ios_base::in)))
            return traits_type::eof();

        /* NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic) */
        auto newoff = off_type{traits_type::eof()};
        switch(whence)
        {
        case std::ios_base::beg:
            if(offset < 0 or offset > std::ssize(mData))
                return traits_type::eof();
            newoff = offset;
            break;

        case std::ios_base::cur:
            if((mode&std::ios_base::out))
            {
                if((mode&std::ios_base::in))
                    return traits_type::eof();

                if((offset >= 0 and offset > this->epptr()-this->pptr())
                    or (offset < 0 and -offset > this->pptr()-this->pbase()))
                    return traits_type::eof();
                newoff = this->pptr() - this->pbase() + offset;
            }
            else if((mode&std::ios_base::in))
            {
                if((offset >= 0 and offset > this->egptr()-this->gptr())
                    or (offset < 0 and -offset > this->gptr()-this->eback()))
                    return traits_type::eof();
                newoff = this->gptr() - this->eback() + offset;
            }
            break;

        case std::ios_base::end:
            {
                auto const base = (mode == std::ios_base::out) ? this->pptr() - this->pbase()
                    : std::ssize(mData);
                if(offset > 0 or -offset > base)
                    return traits_type::eof();
                newoff = base + offset;
            }
            break;

        default:
            return traits_type::eof();
        }

        if(newoff >= 0 and newoff <= std::ssize(mData))
        {
            if((mode&std::ios_base::in))
                this->setg(this->eback(), this->eback()+newoff, this->egptr());
            if((mode&std::ios_base::out))
            {
                this->setp(this->pbase(), this->epptr());
                this->pbump(al::saturate_cast<int>(newoff));
            }
        }
        /* NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic) */

        return newoff;
    }

    auto seekpos(pos_type const pos, std::ios_base::openmode const mode) -> pos_type final
    {
        return seekoff(pos, std::ios_base::beg, mode);
    }
};
using spanbuf = basic_spanbuf<char>;

template<typename CharT, typename Traits>
auto swap(basic_spanbuf<CharT, Traits>& lhs, basic_spanbuf<CharT, Traits>& rhs) noexcept -> void
{ lhs.swap(rhs); }


template<typename CharT, typename Traits = std::char_traits<CharT>>
class basic_ispanstream final : public std::basic_istream<CharT, Traits> {
    basic_spanbuf<CharT, Traits> mStreamBuf;

    using istream_type = std::basic_istream<CharT, Traits>;

public:
    using char_type   = CharT;
    using int_type    = typename Traits::int_type;
    using pos_type    = typename Traits::pos_type;
    using off_type    = typename Traits::off_type;
    using traits_type = Traits;

    static_assert(std::same_as<char_type, typename traits_type::char_type>);

    explicit
    basic_ispanstream(std::span<CharT> const data,
        std::ios_base::openmode const which = std::ios_base::in)
        : istream_type{nullptr}, mStreamBuf{data, (which&~std::ios_base::out)|std::ios_base::in}
    { this->init(std::addressof(mStreamBuf)); }

    template<std::ranges::borrowed_range ROS>
        requires (not std::convertible_to<ROS, std::span<CharT>>)
            and std::convertible_to<ROS, std::span<CharT const>>
        explicit
    basic_ispanstream(ROS&& r) : istream_type{nullptr}, mStreamBuf{std::ios_base::in}
    {
        auto const s = std::span<CharT const>{std::forward<ROS>(r)};
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) */
        mStreamBuf.span(std::span{const_cast<CharT*>(s.data()), s.size()});
        this->init(std::addressof(mStreamBuf));
    }

    basic_ispanstream(basic_ispanstream const&) = delete;
    basic_ispanstream(basic_ispanstream&& rhs) noexcept
        : istream_type{std::move(rhs)}, mStreamBuf{std::move(rhs.mStreamBuf)}
    { istream_type::set_rdbuf(std::addressof(mStreamBuf)); }

    auto operator=(const basic_ispanstream&) -> basic_ispanstream& = delete;
    auto operator=(basic_ispanstream&& rhs) noexcept LIFETIMEBOUND -> basic_ispanstream&
    {
        if(this != &rhs)
            basic_ispanstream{std::move(rhs)}.swap(*this);
        return *this;
    }

    auto swap(basic_ispanstream& rhs) noexcept -> void
    {
        istream_type::swap(rhs);
        mStreamBuf.swap(rhs.mStreamBuf);
    }

    [[nodiscard]]
    auto rdbuf() const noexcept LIFETIMEBOUND -> basic_spanbuf<CharT, Traits>*
    {
        /* NOLINENEXTLINE(cppcoreguidelines-pro-type-const-cast) */
        return const_cast<basic_spanbuf<CharT, Traits>*>(std::addressof(mStreamBuf));
    }

    [[nodiscard]]
    auto span() const noexcept -> std::span<const CharT> { return mStreamBuf.span(); }

    auto span(std::span<CharT> data) noexcept -> void { return mStreamBuf.span(data); }

    template<std::ranges::borrowed_range ROS>
        requires (not std::convertible_to<ROS, std::span<CharT>>)
            and std::convertible_to<ROS, std::span<CharT const>>
    auto span(ROS&& r) noexcept -> void
    {
        auto const s = std::span<CharT const>{std::forward<ROS>(r)};
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) */
        return mStreamBuf.span(std::span{const_cast<CharT*>(s.data()), s.size()});
    }
};
using ispanstream = basic_ispanstream<char>;

template<typename CharT, typename Traits>
auto swap(basic_ispanstream<CharT, Traits>& lhs, basic_ispanstream<CharT, Traits>& rhs) noexcept
    -> void
{ lhs.swap(rhs); }

} /* namespace al */

#endif /* AL_SPANSTREAM_HPP */
