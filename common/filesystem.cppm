module;

#include "filesystem.h"

export module filesystem;

export namespace fs {
    using fs::copy_options;
    using fs::current_path;
    using fs::directory_iterator;
    using fs::directory_entry;
    using fs::directory_options;
    using fs::exists;
    using fs::file_status;
    using fs::file_time_type;
    using fs::file_type;
    using fs::filesystem_error;
    using fs::path;
    using fs::perm_options;
    using fs::perms;
    using fs::read_symlink;
    using fs::recursive_directory_iterator;
    using fs::space_info;
    using fs::status;

    using fs::absolute;
    using fs::canonical;
    using fs::copy;
    using fs::copy_file;
    using fs::copy_symlink;
    using fs::create_directory;
    using fs::create_directories;
    using fs::create_hard_link;
    using fs::create_symlink;
    using fs::create_directory_symlink;
    using fs::current_path;
    using fs::exists;
    using fs::equivalent;
    using fs::file_size;
    using fs::hard_link_count;
    using fs::last_write_time;
    using fs::permissions;
    using fs::proximate;
    using fs::read_symlink;
    using fs::relative;
    using fs::remove;
    using fs::remove_all;
    using fs::rename;
    using fs::resize_file;
    using fs::space;
    using fs::status;
    using fs::symlink_status;
    using fs::temp_directory_path;
    using fs::weakly_canonical;

    using fs::is_block_file;
    using fs::is_character_file;
    using fs::is_directory;
    using fs::is_empty;
    using fs::is_fifo;
    using fs::is_other;
    using fs::is_regular_file;
    using fs::is_socket;
    using fs::is_symlink;
    using fs::status_known;

    using fs::ofstream;
    using fs::ifstream;
    using fs::fstream;
}

/* Functions (and operator overloads) for argument-dependent lookup (ADL) need
 * to be exported in their original namespace.
 */
#ifdef GHC_USE_STD_FS
export namespace std::filesystem {
    using std::filesystem::begin;
    using std::filesystem::end;

    using std::filesystem::operator~;
    using std::filesystem::operator&;
    using std::filesystem::operator|;
    using std::filesystem::operator^;
    using std::filesystem::operator&=;
    using std::filesystem::operator|=;
    using std::filesystem::operator^=;
}

#else

export namespace ghc::filesystem {
    using ghc::filesystem::begin;
    using ghc::filesystem::end;

    using ghc::filesystem::swap;
    using ghc::filesystem::hash_value;

    using ghc::filesystem::operator~;
    using ghc::filesystem::operator&;
    using ghc::filesystem::operator|;
    using ghc::filesystem::operator^;
    using ghc::filesystem::operator&=;
    using ghc::filesystem::operator|=;
    using ghc::filesystem::operator^=;

    using ghc::filesystem::operator<=>;
    using ghc::filesystem::operator==;
    using ghc::filesystem::operator!=;
    using ghc::filesystem::operator<;
    using ghc::filesystem::operator<=;
    using ghc::filesystem::operator>;
    using ghc::filesystem::operator>=;
    using ghc::filesystem::operator/;
    using ghc::filesystem::operator<<;
    using ghc::filesystem::operator>>;
}
#endif
