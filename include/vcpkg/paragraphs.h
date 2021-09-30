#pragma once

#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/expected.h>

#include <vcpkg/binaryparagraph.h>

namespace vckpg::Parse
{
    struct ParseControlErrorInfo;
}

namespace vcpkg::Paragraphs
{
    using Paragraph = Parse::Paragraph;

    ExpectedS<Paragraph> parse_single_paragraph(StringView str, StringView origin);
    ExpectedS<Paragraph> get_single_paragraph(const Filesystem& fs, const Path& control_path);

    ExpectedS<std::vector<Paragraph>> get_paragraphs(const Filesystem& fs, const Path& control_path);

    ExpectedS<std::vector<Paragraph>> parse_paragraphs(StringView str, StringView origin);

    bool is_port_directory(const Filesystem& fs, const Path& maybe_directory);

    Parse::ParseExpected<SourceControlFile> try_load_port(const Filesystem& fs, const Path& port_directory);
    Parse::ParseExpected<SourceControlFile> try_load_port_text(const std::string& text,
                                                               StringView origin,
                                                               bool is_manifest);

    ExpectedS<BinaryControlFile> try_load_cached_package(const VcpkgPaths& paths, const PackageSpec& spec);

    struct LoadResults
    {
        std::vector<SourceControlFileAndLocation> paragraphs;
        std::vector<std::unique_ptr<Parse::ParseControlErrorInfo>> errors;
    };

    // this allows one to pass this around as an overload set to stuff like `Util::fmap`,
    // as opposed to making it a function
    constexpr struct
    {
        const std::string& operator()(const SourceControlFileAndLocation* loc) const
        {
            return (*this)(*loc->source_control_file);
        }
        const std::string& operator()(const SourceControlFileAndLocation& loc) const
        {
            return (*this)(*loc.source_control_file);
        }
        const std::string& operator()(const SourceControlFile& scf) const { return scf.core_paragraph->name; }
    } get_name_of_control_file;

    LoadResults try_load_all_registry_ports(const VcpkgPaths& paths);

    std::vector<SourceControlFileAndLocation> load_all_registry_ports(const VcpkgPaths& paths);
    std::vector<SourceControlFileAndLocation> load_overlay_ports(const Filesystem& fs, const Path& dir);
}