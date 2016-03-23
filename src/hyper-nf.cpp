// -*- c-basic-offset: 4 -*-
/*
 * hyper-nf.cpp
 *
 * Bootstraps Hyper-NF, a Click-based NFV Chain Synthesizer.
 *
 * Copyright (c) 2015-2016 KTH Royal Institute of Technology
 * Copyright (c) 2015-2016 Georgios Katsikas, Marcel Enguehard
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include "logger/logger.hpp"
#include "shared/helpers.hpp"
#include "generator/generator.hpp"
#include "synthesizer/synthesizer.hpp"

void
usage(Logger &main_log, const char* program) {
	error_chatter(main_log, "Usage: " << program << " -p [propertyFile] [-v]");
	exit(WRONG_INPUT_ARGS);
}

void
version_report(const std::string &version) {
	std::cout << "Hyper-NF "+ version + "\n" << std::endl;
	std::cout << "Copyright (C) 2015-2016 Georgios Katsikas, Marcel Enguehard.\n\
Copyright (C) 2015-2016 KTH Royal Institute of Technology.\n\
\n\
This program is free software: you can redistribute it and/or modify\n\
it under the terms of the GNU General Public License as published by\n\
the Free Software Foundation, either version 3 of the License, or\n\
(at your option) any later version.\n\
\n\
This program is distributed in the hope that it will be useful,\n\
but WITHOUT ANY WARRANTY; without even the implied warranty of\n\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n\
GNU General Public License for more details.\n\
\n\
You should have received a copy of the GNU General Public License\n\
along with this program.  If not, see <http://www.gnu.org/licenses/>" << std::endl;
	exit(SUCCESS);
}

bool
parse_arguments(
		int         cmd_args_no,
		char        **cmd_args,
		Logger      &main_log,
		std::string *property_file,
		std::string *version) {

	const char *program = cmd_args[0];

	// Check number of arguments
	if ( (cmd_args_no != 2) && (cmd_args_no != 3 ) && (cmd_args_no != 4) ) {
		usage(main_log, program);
	}

	// Parse arguments
	while ( *cmd_args )	{
		// Property file
		if ( strcmp(*cmd_args, "-p") == 0 )	{

			if (cmd_args_no == 2) {
				usage(main_log, program);
			}

			cmd_args++;
			if ( *cmd_args ) {
				*property_file = (std::string) *cmd_args;
				continue;
			}
			else {
				break;
			}
		}
		// Hyper-NF version
		else if ( strcmp(*cmd_args, "-v") == 0 ) {

			#ifdef VERSION
				*version = " v." + std::string(VERSION);
			#else
				error_chatter(main_log, "Hyper-NF version not available. Autoconf bug");
				exit(FAILURE);
			#endif

			if (cmd_args_no == 2) {
				version_report(*version);
			}
			cmd_args++;
		}
		else {
			// Ignore irrelevant arguments
			cmd_args++;
		}
	}

	*cmd_args = NULL;

	return DONE;
}

int
main(int argc, char **argv) {
	setvbuf(stdout, NULL, _IONBF, 0);

	std::string property_file;
	std::string version("");
	short       exit_status     = 0;
	int         task_exec_time  = 0;
	int         total_exec_time = 0;

	// Initialize logger
	Logger main_log(__FILE__);

	// Check input arguments validity
	if ( !(exit_status=parse_arguments(argc, argv, main_log, &property_file, &version)) ) {
		exit(exit_status);
	}

	info_chatter(main_log, "Hyper-NF" + version + ": A synthesizer of Click-based chained Network Functions");

	//////////////////////////////////// Load property file ///////////////////////////////////
	ParserConfiguration* pcf = NULL;

	info_chatter(main_log, "");
	info_chatter(main_log, "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
	task_exec_time = measure<>::execution( [&]() {
		info_chatter(main_log, "Hyper-NF Chain Loader... ");
		pcf = new ParserConfiguration(property_file);
		def_chatter(main_log, "\tProperty file: " << property_file);
		if ( !(exit_status=pcf->load_property_file()) ) {
			delete pcf;
			exit(exit_status);
		}
		pcf->get_chain()->print_vertex_order();
	});
	info_chatter(main_log, task_exec_time << "  microseconds");
	info_chatter(main_log, "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
	info_chatter(main_log, "");
	///////////////////////////////////////////////////////////////////////////////////////////

	total_exec_time += task_exec_time;

	////////////////////////////////////// Parse Input NFs ////////////////////////////////////
	ChainParser* parser = NULL;

	info_chatter(main_log, "");
	info_chatter(main_log, "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
	task_exec_time = measure<>::execution( [&]() {
		info_chatter(main_log, "Hyper-NF Chain Parser... ");
		try {
			parser = new ChainParser(std::move(pcf));
		}
		catch (const std::exception& e) {
			error_chatter(main_log, "|--> " << e.what());
			delete pcf;
			exit(CHAIN_PARSING_PROBLEM);
		}

		// 1. Load and verify all the Click configuration of the chain
		if ( !(exit_status=parser->load_nf_configurations()) ) {
			delete parser;
			exit(exit_status);
		}

		// 2. Link the edges of all NF DAGs so as to prepare the Traffic Class Builder
		if ( !(exit_status=parser->chain_nf_configurations()) ) {
			delete parser;
			exit(exit_status);
		}
	});
	info_chatter(main_log, task_exec_time << "  microseconds");
	info_chatter(main_log, "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
	info_chatter(main_log, "");
	///////////////////////////////////////////////////////////////////////////////////////////

	total_exec_time += task_exec_time;

	/////////////////////////////////// Build Traffic Classes /////////////////////////////////
	Synthesizer* synthesizer = NULL;

	info_chatter(main_log, "");
	info_chatter(main_log, "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
	task_exec_time = measure<>::execution( [&]() {
		info_chatter(main_log, "Hyper-NF Traffic Class Builder... ");
		try {
			synthesizer = new Synthesizer(std::move(parser));
		}
		catch (const std::exception& e) {
			error_chatter(main_log, "|--> " << e.what());
			delete parser;
			exit(CHAIN_SYNTHESIS_PROBLEM);
		}

		if ( !(exit_status=synthesizer->build_traffic_classes()) ) {
			delete synthesizer;
			exit(exit_status);
		}
		//synthesizer->test_traffic_class_builder();
	});
	info_chatter(main_log, task_exec_time << "  microseconds");
	info_chatter(main_log, "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
	info_chatter(main_log, "");
	///////////////////////////////////////////////////////////////////////////////////////////

	total_exec_time += task_exec_time;

	///////////////////////////////////////// Synthesize //////////////////////////////////////
	info_chatter(main_log, "");
	info_chatter(main_log, "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
	task_exec_time = measure<>::execution( [&]() {
		info_chatter(main_log, "Hyper-NF Synthesizer... ");
		if ( !(exit_status=synthesizer->synthesize_nat()) ) {
			delete synthesizer;
			exit(exit_status);
		}
	});
	info_chatter(main_log, task_exec_time << "  microseconds");
	info_chatter(main_log, "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
	info_chatter(main_log, "");
	///////////////////////////////////////////////////////////////////////////////////////////	

	total_exec_time += task_exec_time;

	////////////////////////////// Generate Hyper-NF Configuration ////////////////////////////
	Generator* generator = NULL;
	info_chatter(main_log, "");
	info_chatter(main_log, "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
	task_exec_time = measure<>::execution( [&]() {
		info_chatter(main_log, "Hyper-NF Generator... ");
		try {
			generator = new Generator(std::move(synthesizer));
		}
		catch (const std::exception& e) {
			error_chatter(main_log, "|--> " << e.what());
			delete synthesizer;
			exit(CHAIN_SYNTHESIS_PROBLEM);
		}

		if ( !(exit_status=generator->generate_equivalent_configuration()) ) {
			delete generator;
			exit(exit_status);
		}
	});
	info_chatter(main_log, task_exec_time << "  microseconds");
	info_chatter(main_log, "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
	info_chatter(main_log, "");
	///////////////////////////////////////////////////////////////////////////////////////////

	total_exec_time += task_exec_time;

	///////////////////////////////////////// Clean-Up ////////////////////////////////////////
	// Domino destruction starts from Generator's destructor destroying the nested objects:
	// Generator:
	// |--> Synthesizer
	// |----|--> ChainParser
	// |---------|--> ParserConfiguration   <-- Destoy first and climb
	//
	info_chatter(main_log, "");
	info_chatter(main_log, "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
	task_exec_time = measure<>::execution( [&]()	{
		info_chatter(main_log, "Hyper-NF Harvester... ");
		delete generator;
	});
	info_chatter(main_log, task_exec_time << "  microseconds");
	info_chatter(main_log, "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
	info_chatter(main_log, "");
	///////////////////////////////////////////////////////////////////////////////////////////

	total_exec_time += task_exec_time;

	info_chatter(main_log, "");
	note_chatter(main_log, "=================================================================================");
	note_chatter(main_log, "=== Total Execution Time: " + 
							std::to_string(float(total_exec_time)/1000) + 
							" milliseconds");
	note_chatter(main_log, "=================================================================================");

	exit(SUCCESS);
}
