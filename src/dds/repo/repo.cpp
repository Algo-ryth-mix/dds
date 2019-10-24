#include "./repo.hpp"

#include <dds/sdist.hpp>
#include <dds/util/paths.hpp>
#include <dds/util/string.hpp>

#include <spdlog/spdlog.h>

#include <range/v3/action/join.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/transform.hpp>

using namespace dds;

using namespace ranges;

void repository::_log_blocking(path_ref dirpath) noexcept {
    spdlog::warn("Another process has the repository directory locked [{}]", dirpath.string());
    spdlog::warn("Waiting for repository to be released...");
}

void repository::_init_repo_dir(path_ref dirpath) noexcept {
    fs::create_directories(dirpath / "dist");
}

fs::path repository::default_local_path() noexcept { return dds_data_dir() / "repo"; }

repository repository::open_for_directory(path_ref dirpath) {
    auto dist_dir = dirpath / "dist";
    auto entries  = fs::directory_iterator(dist_dir) | to_vector;
    return {dirpath};
}

void repository::add_sdist(const sdist& sd) {
    auto sd_dest
        = _root / "dist" / sd.manifest.name / sd.manifest.version.to_string() / sd.md5_string();
    if (fs::exists(sd_dest)) {
        spdlog::info("Source distribution '{}' is already available in the local repo",
                     sd.path.string());
        return;
    }
    auto tmp_copy = sd_dest;
    tmp_copy.replace_filename(".tmp-import");
    if (fs::exists(tmp_copy)) {
        fs::remove_all(tmp_copy);
    }
    fs::create_directories(tmp_copy.parent_path());
    fs::copy(sd.path, tmp_copy, fs::copy_options::recursive);
    fs::rename(tmp_copy, sd_dest);
    spdlog::info("Source distribution '{}' successfully exported", sd.ident());
}

std::vector<sdist> repository::load_sdists() const noexcept {
    using namespace ranges;
    using namespace ranges::views;
    auto drop_dot_dirs
        = filter([](path_ref p) { return !starts_with(p.filename().string(), "."); });

    auto iter_children = [&](path_ref p) { return fs::directory_iterator(p) | drop_dot_dirs; };

    auto try_read_sdist = [](path_ref p) -> std::optional<sdist> {
        try {
            return sdist::from_directory(p);
        } catch (const std::runtime_error& e) {
            spdlog::error("Failed to load source distribution from directory '{}': {}",
                          p.string(),
                          e.what());
            return std::nullopt;
        }
    };

    return
        // Get the top-level `name` dirs
        fs::directory_iterator(_dist_dir())  //
        // Get the next level `version` dirs
        | transform(iter_children)  //
        | views::join               //
        // Get the next level `ident` dirs
        | transform(iter_children)  //
        | views::join               //
        // // Convert each dir into an `sdist` object
        | transform(try_read_sdist)  //
        // // Drop items that failed to load
        | filter([](auto&& opt) { return opt.has_value(); })  //
        | transform([](auto&& opt) { return *opt; })          //
        | to_vector                                           //
        ;
}

std::optional<sdist> repository::get_sdist(std::string_view name, std::string_view version) const {
    auto expect_path = _dist_dir() / name / version;
    if (!fs::is_directory(expect_path)) {
        return std::nullopt;
    }

    auto dir_iter = fs::directory_iterator(expect_path);
    if (dir_iter == fs::directory_iterator()) {
        return std::nullopt;
    }
    return sdist::from_directory(*dir_iter);
}