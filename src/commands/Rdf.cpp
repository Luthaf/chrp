/* cfiles, an analysis frontend for the Chemfiles library
 * Copyright (C) 2015 Guillaume Fraux
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/
*/
#include <docopt/docopt.h>

#include "Rdf.hpp"
#include "Errors.hpp"

using namespace chemfiles;

static const double pi = 3.141592653589793238463;
static const char OPTIONS[] =
R"(cfiles rdf: compute radial distribution function

Compute pair radial distrubution function (often called g(r)). The pairs of
particles to use can be specified using the chemfiles selection language. It
is possible to provide an alternative topology or unit cell when this
information is not present in the trajectory.

Usage:
  cfiles rdf [options] <trajectory>
  cfiles rdf (-h | --help)

Options:
  -h --help                     show this help
  -o <file>, --output=<file>    write result to <file>. This default to the
                                trajectory file name with the `.rdf` extension.
  -s <sel>, --selection=<sel>   selection to use for the atoms. This can be a
                                single selection ("name O") or a selection of
                                two atoms ("pairs: name($1) O and name($2) H")
                                [default: all]
  --max=<max>                   maximal distance to use [default: 10]
  -p <n>, --points=<n>          number of points in the histogram [default: 200])";

std::string Rdf::description() const {
    return "Compute radial distribution functions";
}

std::string Rdf::help() const {
    return OPTIONS;
}

void Rdf::setup(int argc, const char* argv[], Histogram<double>& histogram) {
    auto options = std::string(OPTIONS) + AverageCommand::AVERAGE_OPTIONS;
    auto args = docopt::docopt(options, {argv, argv + argc}, true, "");

    AverageCommand::parse_options(args);

    if (args["--output"]){
        options_.outfile = args["--output"].asString();
    } else {
        options_.outfile = AverageCommand::options().trajectory + ".rdf";
    }

    options_.rmax = stod(args["--max"].asString());
    options_.npoints = stol(args["--points"].asString());
    options_.selection = args["--selection"].asString();


    if (AverageCommand::options().custom_cell) {
        auto& cell = AverageCommand::options().cell;
        double L = std::min(cell.a(), std::min(cell.b(), cell.c()));
        options_.rmax = L/2;
	}

    histogram = Histogram<double>(options_.npoints, 0, options_.rmax);
    selection_ = Selection(options_.selection);
    if (selection_.size() > 2) {
        throw CFilesError("Can not use a selection with more than two atoms in RDF.");
    }
}

void Rdf::finish(const Histogram<double>& histogram) {
    std::ofstream outfile(options_.outfile, std::ios::out);
    if(outfile.is_open()) {
        outfile << "# Radial distribution function in trajectory " << AverageCommand::options().trajectory << std::endl;
        outfile << "# Selection: " << options_.selection << std::endl;

        double dr = histogram.bin_size();
        for (size_t i=0; i<histogram.size(); i++){
            outfile << i * dr << "  " << histogram[i] << "\n";
        }
    } else {
        throw CFilesError("Could not open the '" + options_.outfile + "' file.");
    }
}

void Rdf::accumulate(const Frame& frame, Histogram<double>& histogram) {
    auto positions = frame.positions();
    auto cell = frame.cell();
    size_t npairs = 0;

    if (selection_.size() == 1) {
        // If we have a single atom selection, use it for both atoms of the
        // pairs
        auto matched = selection_.list(frame);
        for (auto i: matched) {
            for (auto j: matched) {
                if (i == j) continue;

                auto rij = norm(cell.wrap(positions[j] - positions[i]));
                if (rij < options_.rmax){
                    histogram.insert_at(rij);
                    npairs++;
                }
            }
    	}
    } else {
        // If we have a pair selection, use it directly
        assert(selection_.size() == 2);
        auto matched = selection_.evaluate(frame);

        for (auto match: matched) {
    		auto i = match[0];
    		auto j = match[1];
            auto rij = norm(cell.wrap(positions[j] - positions[i]));
            if (rij < options_.rmax){
                histogram.insert_at(rij);
                npairs++;
            }
    	}
    }

    // Normalize the rdf to be 1 at long distances
    double V = cell.volume();
    if (V > 0) V = 1;

    double dr = histogram.bin_size();
    double rho = frame.natoms() / V;
    double norm = 1e-6 * 2 * 4 * pi * rho * npairs * dr;

    histogram.normalize([norm, dr](size_t i, double val){
        double r = (i + 0.5) * dr;
        return val / (norm * r * r);
    });
}