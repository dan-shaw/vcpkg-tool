
#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/system.print.h>

#include <vcpkg/commands.add-version.h>
#include <vcpkg/configuration.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versions.h>

using namespace vcpkg;

namespace
{
    constexpr StringLiteral BASELINE = "baseline";
    constexpr StringLiteral VERSION_RELAXED = "version";
    constexpr StringLiteral VERSION_SEMVER = "version-semver";
    constexpr StringLiteral VERSION_DATE = "version-date";
    constexpr StringLiteral VERSION_STRING = "version-string";

    static constexpr StringLiteral OPTION_ALL = "all";
    static constexpr StringLiteral OPTION_OVERWRITE_VERSION = "overwrite-version";
    static constexpr StringLiteral OPTION_SKIP_FORMATTING_CHECK = "skip-formatting-check";
    static constexpr StringLiteral OPTION_SKIP_VERSION_FORMAT_CHECK = "skip-version-format-check";
    static constexpr StringLiteral OPTION_VERBOSE = "verbose";

    enum class UpdateResult
    {
        Updated,
        NotUpdated
    };

    DECLARE_AND_REGISTER_MESSAGE(
        AddVersionSuggestNewVersionScheme,
        (msg::new_scheme, msg::old_scheme, msg::package_name, msg::option),
        "The -- before {option} must be preserved as they're part of the help message for the user.",
        "Use the version scheme \"{new_scheme}\" instead of \"{old_scheme}\" in port "
        "\"{package_name}\".\nUse --{option} to disable this check.");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionVersionAlreadyInFile,
                                 (msg::version, msg::path),
                                 "",
                                 "version {version} is already in {path}");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionAddedVersionToFile,
                                 (msg::version, msg::path),
                                 "",
                                 "added version {version} to {path}");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionNewFile, (), "", "(new file)");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionUncommittedChanges,
                                 (msg::package_name),
                                 "",
                                 "there are uncommitted changes for {package_name}");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionPortFilesShaUnchanged,
                                 (msg::package_name, msg::version),
                                 "",
                                 "checked-in files for {package_name} are unchanged from version {version}");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionCommitChangesReminder, (), "", "Did you remember to commit your changes?");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionNoFilesUpdated, (), "", "No files were updated");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionNoFilesUpdatedForPort,
                                 (msg::package_name),
                                 "",
                                 "No files were updated for {package_name}");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionPortFilesShaChanged,
                                 (msg::package_name),
                                 "",
                                 "checked-in files for {package_name} have changed but the version was not updated");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionVersionIs, (msg::version), "", "version: {version}");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionOldShaIs,
                                 (msg::value),
                                 "{value} is a 40-digit hexadecimal SHA",
                                 "old SHA: {value}");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionNewShaIs,
                                 (msg::value),
                                 "{value} is a 40-digit hexadecimal SHA",
                                 "new SHA: {value}");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionUpdateVersionReminder,
                                 (),
                                 "",
                                 "Did you remember to update the version or port version?");
    DECLARE_AND_REGISTER_MESSAGE(
        AddVersionOverwriteOptionSuggestion,
        (msg::option),
        "The -- before {option} must be preserved as they're part of the help message for the user.",
        "Use --{option} to bypass this check");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionUnableToParseVersionsFile,
                                 (msg::path),
                                 "",
                                 "unable to parse versions file {path}");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionFileNotFound, (msg::path), "", "couldn't find required file {path}");
    DECLARE_AND_REGISTER_MESSAGE(
        AddVersionIgnoringOptionAll,
        (msg::option),
        "The -- before {option} must be preserved as they're part of the help message for the user.",
        "ignoring --{option} since a port name argument was provided");
    DECLARE_AND_REGISTER_MESSAGE(
        AddVersionUseOptionAll,
        (msg::command_name, msg::option),
        "The -- before {option} must be preserved as they're part of the help message for the user.",
        "{command_name} with no arguments requires passing --{option} to update all port versions at once");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionLoadPortFailed, (msg::package_name), "", "can't load port {package_name}");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionPortHasImproperFormat,
                                 (msg::package_name),
                                 "",
                                 "{package_name} is not properly formatted");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionFormatPortSuggestion,
                                 (msg::command_line),
                                 "",
                                 "Run `{command_line}` to format the file");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionCommitResultReminder, (), "", "Don't forget to commit the result!");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionNoGitSha,
                                 (msg::package_name),
                                 "",
                                 "can't obtain SHA for port {package_name}");
    DECLARE_AND_REGISTER_MESSAGE(AddVersionPortDoesNotExist, (msg::package_name), "", "{package_name} does not exist");

    using VersionGitTree = std::pair<SchemedVersion, std::string>;

    void insert_version_to_json_object(Json::Object& obj, const Version& version, StringLiteral version_field)
    {
        obj.insert(version_field, Json::Value::string(version.text()));
        obj.insert("port-version", Json::Value::integer(version.port_version()));
    }

    void insert_schemed_version_to_json_object(Json::Object& obj, const SchemedVersion& version)
    {
        if (version.scheme == VersionScheme::Relaxed)
        {
            return insert_version_to_json_object(obj, version.version, VERSION_RELAXED);
        }

        if (version.scheme == VersionScheme::Semver)
        {
            return insert_version_to_json_object(obj, version.version, VERSION_SEMVER);
        }

        if (version.scheme == VersionScheme::Date)
        {
            return insert_version_to_json_object(obj, version.version, VERSION_DATE);
        }

        if (version.scheme == VersionScheme::String)
        {
            return insert_version_to_json_object(obj, version.version, VERSION_STRING);
        }
        Checks::unreachable(VCPKG_LINE_INFO);
    }

    void check_used_version_scheme(const SchemedVersion& version, const std::string& port_name)
    {
        if (version.scheme == VersionScheme::String)
        {
            if (DateVersion::try_parse(version.version.text()))
            {
                Checks::msg_exit_with_message(VCPKG_LINE_INFO,
                                              msgAddVersionSuggestNewVersionScheme,
                                              msg::new_scheme = VERSION_DATE,
                                              msg::old_scheme = VERSION_STRING,
                                              msg::package_name = port_name,
                                              msg::option = OPTION_SKIP_VERSION_FORMAT_CHECK);
            }
            if (DotVersion::try_parse_relaxed(version.version.text()))
            {
                Checks::msg_exit_with_message(VCPKG_LINE_INFO,
                                              msgAddVersionSuggestNewVersionScheme,
                                              msg::new_scheme = VERSION_RELAXED,
                                              msg::old_scheme = VERSION_STRING,
                                              msg::package_name = port_name,
                                              msg::option = OPTION_SKIP_VERSION_FORMAT_CHECK);
            }
        }
    }

    static Json::Object serialize_baseline(const std::map<std::string, Version, std::less<>>& baseline)
    {
        Json::Object port_entries_obj;
        for (auto&& kv_pair : baseline)
        {
            Json::Object baseline_version_obj;
            insert_version_to_json_object(baseline_version_obj, kv_pair.second, BASELINE);
            port_entries_obj.insert(kv_pair.first, baseline_version_obj);
        }

        Json::Object baseline_obj;
        baseline_obj.insert("default", port_entries_obj);
        return baseline_obj;
    }

    static Json::Object serialize_versions(const std::vector<VersionGitTree>& versions)
    {
        Json::Array versions_array;
        for (auto&& version : versions)
        {
            Json::Object version_obj;
            version_obj.insert("git-tree", Json::Value::string(version.second));
            insert_schemed_version_to_json_object(version_obj, version.first);
            versions_array.push_back(std::move(version_obj));
        }

        Json::Object output_object;
        output_object.insert("versions", versions_array);
        return output_object;
    }

    static void write_baseline_file(Filesystem& fs,
                                    const std::map<std::string, Version, std::less<>>& baseline_map,
                                    const Path& output_path)
    {
        auto new_path = output_path + ".tmp";
        fs.create_directories(output_path.parent_path(), VCPKG_LINE_INFO);
        fs.write_contents(new_path,
                          Json::stringify(serialize_baseline(baseline_map), Json::JsonStyle::with_spaces(2)),
                          VCPKG_LINE_INFO);
        fs.rename(new_path, output_path, VCPKG_LINE_INFO);
    }

    static void write_versions_file(Filesystem& fs,
                                    const std::vector<VersionGitTree>& versions,
                                    const Path& output_path)
    {
        auto new_path = output_path + ".tmp";
        fs.create_directories(output_path.parent_path(), VCPKG_LINE_INFO);
        fs.write_contents(
            new_path, Json::stringify(serialize_versions(versions), Json::JsonStyle::with_spaces(2)), VCPKG_LINE_INFO);
        fs.rename(new_path, output_path, VCPKG_LINE_INFO);
    }

    static UpdateResult update_baseline_version(const VcpkgPaths& paths,
                                                const std::string& port_name,
                                                const Version& version,
                                                const Path& baseline_path,
                                                std::map<std::string, vcpkg::Version, std::less<>>& baseline_map,
                                                bool print_success)
    {
        auto& fs = paths.get_filesystem();

        auto it = baseline_map.find(port_name);
        if (it != baseline_map.end())
        {
            auto& baseline_version = it->second;
            if (baseline_version == version)
            {
                if (print_success)
                {
                    msg::println(Color::success,
                                 msgAddVersionVersionAlreadyInFile,
                                 msg::version = version,
                                 msg::path = baseline_path);
                }
                return UpdateResult::NotUpdated;
            }
            baseline_version = version;
        }
        else
        {
            baseline_map.emplace(port_name, version);
        }

        write_baseline_file(fs, baseline_map, baseline_path);
        if (print_success)
        {
            msg::println(
                Color::success, msgAddVersionAddedVersionToFile, msg::version = version, msg::path = baseline_path);
        }
        return UpdateResult::Updated;
    }

    static UpdateResult update_version_db_file(const VcpkgPaths& paths,
                                               const std::string& port_name,
                                               const SchemedVersion& port_version,
                                               const std::string& git_tree,
                                               const Path& version_db_file_path,
                                               bool overwrite_version,
                                               bool print_success,
                                               bool keep_going,
                                               bool skip_version_format_check)
    {
        auto& fs = paths.get_filesystem();
        if (!fs.exists(version_db_file_path, IgnoreErrors{}))
        {
            if (!skip_version_format_check)
            {
                check_used_version_scheme(port_version, port_name);
            }
            std::vector<VersionGitTree> new_entry{{port_version, git_tree}};
            write_versions_file(fs, new_entry, version_db_file_path);
            if (print_success)
            {
                msg::println(Color::success,
                             msg::format(msgAddVersionAddedVersionToFile,
                                         msg::version = port_version.version,
                                         msg::path = version_db_file_path)
                                 .append_raw(" ")
                                 .append(msgAddVersionNewFile));
            }
            return UpdateResult::Updated;
        }

        auto maybe_versions = get_builtin_versions(paths, port_name);
        if (auto versions = maybe_versions.get())
        {
            const auto& versions_end = versions->end();

            auto found_same_sha = std::find_if(
                versions->begin(), versions_end, [&](auto&& entry) -> bool { return entry.second == git_tree; });
            if (found_same_sha != versions_end)
            {
                if (found_same_sha->first.version == port_version.version)
                {
                    if (print_success)
                    {
                        msg::println(Color::success,
                                     msgAddVersionVersionAlreadyInFile,
                                     msg::version = port_version.version,
                                     msg::path = version_db_file_path);
                    }
                    return UpdateResult::NotUpdated;
                }
                msg::print_warning(msg::format(msgAddVersionPortFilesShaUnchanged,
                                               msg::package_name = port_name,
                                               msg::version = found_same_sha->first.version)
                                       .appendnl()
                                       .append_raw("-- SHA: ")
                                       .append_raw(git_tree)
                                       .appendnl()
                                       .append_raw("-- ")
                                       .append(msgAddVersionCommitChangesReminder)
                                       .appendnl()
                                       .append_raw("***")
                                       .append(msgAddVersionNoFilesUpdated)
                                       .append_raw("***"));
                if (keep_going) return UpdateResult::NotUpdated;
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            auto it = std::find_if(
                versions->begin(), versions_end, [&](const std::pair<SchemedVersion, std::string>& entry) -> bool {
                    return entry.first.version == port_version.version;
                });

            if (it != versions_end)
            {
                if (!overwrite_version)
                {
                    msg::print_error(
                        msg::format(msgAddVersionPortFilesShaChanged, msg::package_name = port_name)
                            .appendnl()
                            .append(msgAddVersionVersionIs, msg::version = port_version.version)
                            .appendnl()
                            .append(msgAddVersionOldShaIs, msg::value = it->second)
                            .appendnl()
                            .append(msgAddVersionNewShaIs, msg::value = git_tree)
                            .appendnl()
                            .append(msgAddVersionUpdateVersionReminder)
                            .appendnl()
                            .append(msgAddVersionOverwriteOptionSuggestion, msg::option = OPTION_OVERWRITE_VERSION)
                            .appendnl()
                            .append_raw("***")
                            .append(msgAddVersionNoFilesUpdated)
                            .append_raw("***"));
                    if (keep_going) return UpdateResult::NotUpdated;
                    Checks::exit_fail(VCPKG_LINE_INFO);
                }

                it->first = port_version;
                it->second = git_tree;
            }
            else
            {
                versions->insert(versions->begin(), std::make_pair(port_version, git_tree));
            }

            if (!skip_version_format_check)
            {
                check_used_version_scheme(port_version, port_name);
            }

            write_versions_file(fs, *versions, version_db_file_path);
            if (print_success)
            {
                msg::println(Color::success,
                             msgAddVersionAddedVersionToFile,
                             msg::version = port_version.version,
                             msg::path = version_db_file_path);
            }
            return UpdateResult::Updated;
        }

        msg::print_error(msg::format(msgAddVersionUnableToParseVersionsFile, msg::path = version_db_file_path)
                             .appendnl()
                             .append_raw(maybe_versions.error()));
        Checks::exit_fail(VCPKG_LINE_INFO);
    }
}

namespace vcpkg::Commands::AddVersion
{
    const CommandSwitch COMMAND_SWITCHES[] = {
        {OPTION_ALL, "Process versions for all ports."},
        {OPTION_OVERWRITE_VERSION, "Overwrite `git-tree` of an existing version."},
        {OPTION_SKIP_FORMATTING_CHECK, "Skips the formatting check of vcpkg.json files."},
        {OPTION_SKIP_VERSION_FORMAT_CHECK, "Skips the version format check."},
        {OPTION_VERBOSE, "Print success messages instead of just errors."},
    };

    const CommandStructure COMMAND_STRUCTURE{
        create_example_string(R"###(x-add-version <port name>)###"),
        0,
        1,
        {{COMMAND_SWITCHES}, {}, {}},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed_args = args.parse_arguments(COMMAND_STRUCTURE);
        const bool add_all = Util::Sets::contains(parsed_args.switches, OPTION_ALL);
        const bool overwrite_version = Util::Sets::contains(parsed_args.switches, OPTION_OVERWRITE_VERSION);
        const bool skip_formatting_check = Util::Sets::contains(parsed_args.switches, OPTION_SKIP_FORMATTING_CHECK);
        const bool skip_version_format_check =
            Util::Sets::contains(parsed_args.switches, OPTION_SKIP_VERSION_FORMAT_CHECK);
        const bool verbose = !add_all || Util::Sets::contains(parsed_args.switches, OPTION_VERBOSE);

        auto& fs = paths.get_filesystem();
        auto baseline_path = paths.builtin_registry_versions / "baseline.json";
        if (!fs.exists(baseline_path, IgnoreErrors{}))
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgAddVersionFileNotFound, msg::path = baseline_path);
        }

        std::vector<std::string> port_names;
        if (!args.command_arguments.empty())
        {
            if (add_all)
            {
                msg::print_warning(msgAddVersionIgnoringOptionAll, msg::option = OPTION_ALL);
            }
            port_names.emplace_back(args.command_arguments[0]);
        }
        else
        {
            Checks::msg_check_exit(VCPKG_LINE_INFO,
                                   add_all,
                                   msgAddVersionUseOptionAll,
                                   msg::command_name = "x-add-version",
                                   msg::option = OPTION_ALL);

            for (auto&& port_dir : fs.get_directories_non_recursive(paths.builtin_ports_directory(), VCPKG_LINE_INFO))
            {
                port_names.emplace_back(port_dir.stem().to_string());
            }
        }

        auto baseline_map = [&]() -> std::map<std::string, vcpkg::Version, std::less<>> {
            if (!fs.exists(baseline_path, IgnoreErrors{}))
            {
                std::map<std::string, vcpkg::Version, std::less<>> ret;
                return ret;
            }
            auto maybe_baseline_map = vcpkg::get_builtin_baseline(paths);
            return maybe_baseline_map.value_or_exit(VCPKG_LINE_INFO);
        }();

        // Get tree-ish from local repository state.
        auto maybe_git_tree_map = paths.git_get_local_port_treeish_map();
        auto git_tree_map = maybe_git_tree_map.value_or_exit(VCPKG_LINE_INFO);

        for (auto&& port_name : port_names)
        {
            auto port_dir = paths.builtin_ports_directory() / port_name;

            if (!fs.exists(port_dir, IgnoreErrors{}))
            {
                msg::print_error(msgAddVersionPortDoesNotExist, msg::package_name = port_name);
                Checks::check_exit(VCPKG_LINE_INFO, !add_all);
                continue;
            }

            auto maybe_scf = Paragraphs::try_load_port(fs, paths.builtin_ports_directory() / port_name);
            if (!maybe_scf.has_value())
            {
                msg::print_error(msgAddVersionLoadPortFailed, msg::package_name = port_name);
                print_error_message(maybe_scf.error());
                Checks::check_exit(VCPKG_LINE_INFO, !add_all);
                continue;
            }

            const auto& scf = maybe_scf.value_or_exit(VCPKG_LINE_INFO);

            if (!skip_formatting_check)
            {
                // check if manifest file is property formatted
                const auto path_to_manifest = paths.builtin_ports_directory() / port_name / "vcpkg.json";
                if (fs.exists(path_to_manifest, IgnoreErrors{}))
                {
                    const auto current_file_content = fs.read_contents(path_to_manifest, VCPKG_LINE_INFO);
                    const auto json = serialize_manifest(*scf);
                    const auto formatted_content = Json::stringify(json, {});
                    if (current_file_content != formatted_content)
                    {
                        auto command_line = fmt::format("vcpkg format-manifest ports/{}/vcpkg.json", port_name);
                        msg::print_error(
                            msg::format(msgAddVersionPortHasImproperFormat, msg::package_name = port_name)
                                .appendnl()
                                .append(msgAddVersionFormatPortSuggestion, msg::command_line = command_line)
                                .appendnl()
                                .append(msgAddVersionCommitResultReminder)
                                .appendnl());
                        Checks::check_exit(VCPKG_LINE_INFO, !add_all);
                        continue;
                    }
                }
            }

            // find local uncommitted changes on port
            auto maybe_changes = paths.git_port_has_local_changes(port_name);
            if (maybe_changes.has_value() && maybe_changes.value_or_exit(VCPKG_LINE_INFO))
            {
                msg::print_warning(msgAddVersionUncommittedChanges, msg::package_name = port_name);
            }

            const auto& schemed_version = scf->to_schemed_version();

            auto git_tree_it = git_tree_map.find(port_name);
            if (git_tree_it == git_tree_map.end())
            {
                msg::print_warning(msg::format(msgAddVersionNoGitSha, msg::package_name = port_name)
                                       .appendnl()
                                       .append_raw("-- ")
                                       .append(msgAddVersionCommitChangesReminder)
                                       .appendnl()
                                       .append_raw("***")
                                       .append(msgAddVersionNoFilesUpdated)
                                       .append_raw("***"));
                Checks::check_exit(VCPKG_LINE_INFO, !add_all);
                continue;
            }
            const auto& git_tree = git_tree_it->second;

            char prefix[] = {port_name[0], '-', '\0'};
            auto port_versions_path = paths.builtin_registry_versions / prefix / Strings::concat(port_name, ".json");
            auto updated_versions_file = update_version_db_file(paths,
                                                                port_name,
                                                                schemed_version,
                                                                git_tree,
                                                                port_versions_path,
                                                                overwrite_version,
                                                                verbose,
                                                                add_all,
                                                                skip_version_format_check);
            auto updated_baseline_file = update_baseline_version(
                paths, port_name, schemed_version.version, baseline_path, baseline_map, verbose);
            if (verbose && updated_versions_file == UpdateResult::NotUpdated &&
                updated_baseline_file == UpdateResult::NotUpdated)
            {
                msg::println(msgAddVersionNoFilesUpdatedForPort, msg::package_name = port_name);
            }
        }
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void AddVersionCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        AddVersion::perform_and_exit(args, paths);
    }
}