/*
Copyright 2019 Google Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <cassert>
#include <cstdlib>
#include <cstring>

#include <exception>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "utils.h"

extern "C" {
#include <libjsonnet.h>
#include <libjsonnet_fmt.h>
}

void version(std::ostream &o)
{
    o << "Jsonnet reformatter " << jsonnet_version() << std::endl;
}

void usage(std::ostream &o)
{
    version(o);
    o << "\n";
    o << "jsonnetfmt {<option>} { <filename> }\n";
    o << "Note: Some options do not support multiple filenames\n";
    o << "\n";
    o << "Available options:\n";
    o << "  -h / --help             This message\n";
    o << "  -e / --exec             Treat filename as code\n";
    o << "  -o / --output-file <file> Write to the output file rather than stdout\n";
    o << "  -i / --in-place         Update the Jsonnet file(s) in place.\n";
    o << "  --test                  Exit with failure if reformatting changed the file(s).\n";
    o << "  -n / --indent <n>       Number of spaces to indent by (default 2, 0 means no change)\n";
    o << "  --max-blank-lines <n>   Max vertical spacing, 0 means no change (default 2)\n";
    o << "  --string-style <d|s|l>  Enforce double, single (default) quotes or 'leave'\n";
    o << "  --comment-style <h|s|l> # (h), // (s)(default), or 'leave'; never changes she-bang\n";
    o << "  --[no-]pretty-field-names Use syntax sugar for fields and indexing (on by default)\n";
    o << "  --[no-]pad-arrays       [ 1, 2, 3 ] instead of [1, 2, 3]\n";
    o << "  --[no-]pad-objects      { x: 1, y: 2 } instead of {x: 1, y: 2} (on by default)\n";
    o << "  --[no-]sort-imports     Sorting of imports (on by default)\n";
    o << "  --debug-desugaring      Unparse the desugared AST without executing it\n";
    o << "  --version               Print version\n";
    o << "\n";
    o << "In all cases:\n";
    o << "<filename> can be - (stdin)\n";
    o << "Multichar options are expanded e.g. -abc becomes -a -b -c.\n";
    o << "The -- option suppresses option processing for subsequent arguments.\n";
    o << "Note that since filenames and jsonnet programs can begin with -, it is advised to\n";
    o << "use -- if the argument is unknown, e.g. jsonnet -- \"$FILENAME\".";
    o << std::endl;
}

/** Class for representing configuration read from command line flags.  */
struct JsonnetConfig {
    std::vector<std::string> inputFiles;
    std::string outputFile;
    bool filenameIsCode;

    bool fmtInPlace;
    bool fmtTest;

    JsonnetConfig()
        : filenameIsCode(false),
          fmtInPlace(false),
          fmtTest(false)
    {
    }
};

enum ArgStatus {
    ARG_CONTINUE,
    ARG_SUCCESS,
    ARG_FAILURE,
};

/** Parse the command line arguments, configuring the Jsonnet VM context and
 * populating the JsonnetConfig.
 */
static ArgStatus process_args(int argc, const char **argv, JsonnetConfig *config, JsonnetVm *vm)
{
    auto args = simplify_args(argc, argv);
    std::vector<std::string> remaining_args;

    unsigned i = 0;

    for (; i < args.size(); ++i) {
        const std::string &arg = args[i];
        if (arg == "-h" || arg == "--help") {
            usage(std::cout);
            return ARG_SUCCESS;
        } else if (arg == "-v" || arg == "--version") {
            version(std::cout);
            return ARG_SUCCESS;
        } else if (arg == "-e" || arg == "--exec") {
            config->filenameIsCode = true;
        } else if (arg == "-o" || arg == "--output-file") {
            std::string output_file = next_arg(i, args);
            if (output_file.length() == 0) {
                std::cerr << "ERROR: -o argument was empty string" << std::endl;
                return ARG_FAILURE;
            }
            config->outputFile = output_file;
        } else if (arg == "--") {
            // All subsequent args are not options.
            while ((++i) < args.size())
                remaining_args.push_back(args[i]);          
            break;  
        } else if (arg == "-i" || arg == "--in-place") {
            config->fmtInPlace = true;
        } else if (arg == "--test") {
            config->fmtTest = true;
        } else if (arg == "-n" || arg == "--indent") {
            long l = strtol_check(next_arg(i, args));
            if (l < 0) {
                std::cerr << "ERROR: invalid --indent value: " << l << std::endl;
                return ARG_FAILURE;
            }
            jsonnet_fmt_indent(vm, l);
        } else if (arg == "--max-blank-lines") {
            long l = strtol_check(next_arg(i, args));
            if (l < 0) {
                std::cerr << "ERROR: invalid --max-blank-lines value: " << l << ""
                            << std::endl;
                return ARG_FAILURE;
            }
            jsonnet_fmt_max_blank_lines(vm, l);
        } else if (arg == "--comment-style") {
            const std::string val = next_arg(i, args);
            if (val == "h") {
                jsonnet_fmt_comment(vm, 'h');
            } else if (val == "s") {
                jsonnet_fmt_comment(vm, 's');
            } else if (val == "l") {
                jsonnet_fmt_comment(vm, 'l');
            } else {
                std::cerr << "ERROR: invalid --comment-style value: " << val
                            << std::endl;
                return ARG_FAILURE;
            }
        } else if (arg == "--string-style") {
            const std::string val = next_arg(i, args);
            if (val == "d") {
                jsonnet_fmt_string(vm, 'd');
            } else if (val == "s") {
                jsonnet_fmt_string(vm, 's');
            } else if (val == "l") {
                jsonnet_fmt_string(vm, 'l');
            } else {
                std::cerr << "ERROR: invalid --string-style value: " << val
                            << std::endl;
                return ARG_FAILURE;
            }
        } else if (arg == "--pad-arrays") {
            jsonnet_fmt_pad_arrays(vm, true);
        } else if (arg == "--no-pad-arrays") {
            jsonnet_fmt_pad_arrays(vm, false);
        } else if (arg == "--pad-objects") {
            jsonnet_fmt_pad_objects(vm, true);
        } else if (arg == "--no-pad-objects") {
            jsonnet_fmt_pad_objects(vm, false);
        } else if (arg == "--pretty-field-names") {
            jsonnet_fmt_pretty_field_names(vm, true);
        } else if (arg == "--no-pretty-field-names") {
            jsonnet_fmt_pretty_field_names(vm, false);
        } else if (arg == "--sort-imports") {
            jsonnet_fmt_sort_imports(vm, true);
        } else if (arg == "--no-sort-imports") {
            jsonnet_fmt_sort_imports(vm, false);
        } else if (arg == "--debug-desugaring") {
            jsonnet_fmt_debug_desugaring(vm, true);
        } else if (arg.length() > 1 && arg[0] == '-') {
            std::cerr << "ERROR: unrecognized argument: " << arg << std::endl;
            return ARG_FAILURE;
        } else {
            remaining_args.push_back(args[i]);
        }
    }

    const char *want = config->filenameIsCode ? "code" : "filename";
    if (remaining_args.size() == 0) {
        std::cerr << "ERROR: must give " << want << "\n" << std::endl;
        usage(std::cerr);
        return ARG_FAILURE;
    }

    if (!config->fmtTest && !config->fmtInPlace) {
        if (remaining_args.size() > 1) {
            std::string filename = remaining_args[0];
            std::cerr << "ERROR: only one " << want << " is allowed\n" << std::endl;
            return ARG_FAILURE;
        }
    }
    config->inputFiles = remaining_args;
    return ARG_CONTINUE;
}

int main(int argc, const char **argv)
{
    try {
        JsonnetVm *vm = jsonnet_make();
        JsonnetConfig config;
        ArgStatus arg_status = process_args(argc, argv, &config, vm);
        if (arg_status != ARG_CONTINUE) {
            jsonnet_destroy(vm);
            return arg_status == ARG_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
        }

        // Evaluate input Jsonnet and handle any errors from Jsonnet VM.
        int error;
        char *output;
        std::string output_file = config.outputFile;

        if (config.fmtInPlace || config.fmtTest) {
            assert(config.inputFiles.size() >= 1);
            for (std::string &inputFile : config.inputFiles) {
                if (config.fmtInPlace) {
                    output_file = inputFile;

                    if (inputFile == "-") {
                        std::cerr << "ERROR: cannot use --in-place with stdin" << std::endl;
                        jsonnet_destroy(vm);
                        return EXIT_FAILURE;
                    }
                    if (config.filenameIsCode) {
                        std::cerr << "ERROR: cannot use --in-place with --exec"
                                    << std::endl;
                        jsonnet_destroy(vm);
                        return EXIT_FAILURE;
                    }
                }

                std::string input;
                if (!read_input(config.filenameIsCode, &inputFile, &input)) {
                    jsonnet_destroy(vm);
                    return EXIT_FAILURE;
                }

                output = jsonnet_fmt_snippet(vm, inputFile.c_str(), input.c_str(), &error);

                if (error) {
                    std::cerr << output;
                    jsonnet_realloc(vm, output, 0);
                    jsonnet_destroy(vm);
                    return EXIT_FAILURE;
                }

                if (config.fmtTest) {
                    // Check the output matches the input.
                    bool ok = output == input;
                    jsonnet_realloc(vm, output, 0);
                    if (!ok) {
                        jsonnet_destroy(vm);
                        return 2;
                    }
                } else {
                    // Write output Jsonnet only if there is a difference between input and output
                    bool different = output != input;
                    if (different) {
                        bool successful = write_output_file(output, output_file);
                        jsonnet_realloc(vm, output, 0);
                        if (!successful) {
                            jsonnet_destroy(vm);
                            return EXIT_FAILURE;
                        }
                    }
                }
            }
        } else {
            assert(config.inputFiles.size() == 1);
            // Read input file.
            std::string input;
            if (!read_input(config.filenameIsCode, &config.inputFiles[0], &input)) {
                jsonnet_destroy(vm);
                return EXIT_FAILURE;
            }

            output = jsonnet_fmt_snippet(
                vm, config.inputFiles[0].c_str(), input.c_str(), &error);

            if (error) {
                std::cerr << output;
                jsonnet_realloc(vm, output, 0);
                jsonnet_destroy(vm);
                return EXIT_FAILURE;
            }

            // Write output Jsonnet.
            bool successful = write_output_file(output, output_file);
            jsonnet_realloc(vm, output, 0);
            if (!successful) {
                jsonnet_destroy(vm);
                return EXIT_FAILURE;
            }
        }

        jsonnet_destroy(vm);
        return EXIT_SUCCESS;

    } catch (const std::bad_alloc &) {
        // Avoid further allocation attempts
        fputs("Internal out-of-memory error (please report this)\n", stderr);
    } catch (const std::exception &e) {
        std::cerr << "Internal error (please report this): " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "An unknown exception occurred (please report this)." << std::endl;
    }
    return EXIT_FAILURE;
}
