// cfiles, an analysis frontend for the Chemfiles library
// Copyright (C) Guillaume Fraux and contributors -- BSD license

#include <docopt/docopt.h>
#include <sstream>

#include "Merge.hpp"
#include "Errors.hpp"
#include "utils.hpp"

using namespace chemfiles;

static const char OPTIONS[] =
R"(Merge multiple trajectories into one file. If all trajectories do not have
the same number of steps, the last step of the smaller trajectories is repeated
until the end of the longest trajectory.

Usage:
  cfiles merge [options] (-o <output> | --output=<output>) <input>...
  cfiles merge (-h | --help)

Examples
  cfiles merge solid.pdb gaz.xyz --output=merged.xyz
  cfiles merge --input-format=XYZ,XYZ first.zeo second.zeo -o output.pdb
  cfiles merge -c 25:25:18 polymer.nc surface.xyz -o all.nc

Options:
  -h --help                     show this help
  --input-format=<formats>      comma separated list of formats to use for the
                                input files
  --output-format=<format>      force the output file format to be <format>
  -c <cell>, --cell=<cell>      alternative unit cell. <cell> format is one of
                                <a:b:c:α:β:γ> or <a:b:c> or <a>. 'a', 'b' and
                                'c' are in angstroms, 'α', 'β', and 'γ' are in
                                degrees.
  )";

static Merge::Options parse_options(int argc, const char* argv[]) {
    auto options_str = command_header("merge", Merge().description());
    options_str += "Guillaume Fraux <guillaume@fraux.fr>\n\n";
    options_str += OPTIONS;
    auto args = docopt::docopt(options_str, {argv, argv + argc}, true, "");

    Merge::Options options;
    options.infiles = args["<input>"].asStringList();
    options.outfile = args["<output>"].asString();

    if (args["--input-format"]){
        options.input_formats = split(args["--input-format"].asString(), ',');

        auto n_files = options.infiles.size();
        auto n_formats = options.input_formats.size();
        if (n_files != n_formats) {
            throw cfiles_error(
                "Input formats do not match input files: we have {} file and {} "
                "formats.\n Formats must be provided as a comma separated list: "
                "--input-format='XYZ,PDB,AmberNetCDF'",
                n_files, n_formats
            );
        }
    } else {
        options.input_formats = std::vector<std::string>(options.infiles.size(), "");
    }

    if (args["--output-format"]){
        options.output_format = args["--output-format"].asString();
    }

    if (args["--cell"]) {
        options.custom_cell = true;
        options.cell = parse_cell(args["--cell"].asString());
    }

    return options;
}


std::string Merge::description() const {
    return "merge multiple trajectories";
}

int Merge::run(int argc, const char* argv[]) {
    auto options = parse_options(argc, argv);

    auto inputs = std::vector<Trajectory>();
    for (size_t i=0; i<options.infiles.size(); i++) {
        auto trajectory = Trajectory(options.infiles[i], 'r', options.input_formats[i]);
        inputs.emplace_back(std::move(trajectory));
    }
    auto outfile = Trajectory(options.outfile, 'w', options.output_format);

    if (options.custom_cell) {
        outfile.set_cell(options.cell);
    }

    size_t step = 0;
    auto frames = std::vector<Frame>(inputs.size());
    while (true) {
        bool did_read_one_frame = false;
        for (size_t i=0; i<inputs.size(); i++) {
            // Handle trajectories with different number of steps
            if (step < inputs[i].nsteps()) {
                frames[i] = inputs[i].read();
                did_read_one_frame = true;
            }
        }

        if (!did_read_one_frame) {
            break;
        }

        // Check that unit cells match
        if (!options.custom_cell) {
            // We use the first non-infinite cell as reference
            auto reference = UnitCell();
            for (auto& frame: frames) {
                if (frame.cell().shape() != UnitCell::INFINITE) {
                    reference = frame.cell();
                    break;
                }
            }

            // Either all the cell are the same, or there is one finite cell
            // and multiple infinite cells
            for (auto& frame: frames) {
                if (frame.cell().shape() != UnitCell::INFINITE) {
                    if (frame.cell() != reference) {
                        throw cfiles_error(
                            "Mismatch in unit cells. Please specify which one "
                            "you want using the --cell argument."
                        );
                    }
                }
            }
        }

        bool one_frame_has_velocity = false;
        size_t natoms = 0;
        for (auto& frame: frames) {
            one_frame_has_velocity = one_frame_has_velocity || static_cast<bool>(frame.velocities());
            natoms += frame.size();
        }

        auto output_frame = Frame();
        output_frame.reserve(natoms);
        if (one_frame_has_velocity) {
            output_frame.add_velocities();
        }

        auto start = 0;
        for (auto& frame: frames) {
            auto positions = frame.positions();
            auto& topology = frame.topology();
            for (size_t i=0; i<frame.size(); i++) {
                output_frame.add_atom(topology[i], positions[i]);
            }

            if (frame.velocities()) {
                auto velocities = *frame.velocities();
                auto output_velocities = *output_frame.velocities();
                for (size_t i=0; i<frame.size(); i++) {
                    output_velocities[start + i] = velocities[i];
                }
            }

            // translate bonding informations
            for (auto& bond: frame.topology().bonds()) {
                output_frame.add_bond(start + bond[0], start + bond[1]);
            }

            start += frame.size();
        }

        outfile.write(output_frame);
        step++;
    }

    return 0;
}
