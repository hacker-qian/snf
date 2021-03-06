#ifndef _RSS_GENERATOR_HPP_
#define _RSS_GENERATOR_HPP_

/*
 * rss_generator.hpp
 *
 * Class for exporting a hardware-assisted SNF configuration.
 * A Click-DPDK module is generated with the following properties:
 * |-> Receive-side Scaling (RSS) mechanism to read packets from different
 *     hardware queues of the NIC, using one FromDPDKDevice element per queue.
 * |-> Scheduling of all the FromDPDKDevice elements to perform distributed I/O
 *     across multiple cores.
 * |-> Replication of the chain across multiple cores to perfrom distributed,
 *     parallel processing. Each FromDPDKDevice is linked to a copy of the
 *     synthesized chain.
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

#ifdef HAVE_CONFIG_H
#include "../../config.h"
#endif

#include "generator.hpp"

class RSSGenerator : public Generator {

	private:
		/*
		 * Enum type that indicates how we use multiple cores to do
		 * I/O and processing. Check properties class.
		 */
		IOMode            io_mode;

		/*
		 * Boolean that indicates whether the target system supports
		 * Non-Uniform Memory Access (NUMA).
		 * This helps to do core allocation.
		 */
		bool              numa;

		/*
		 * The NICs to be (potentially) used by multi-core SNF.
		 * In fact, SNF can use up to this number, depending on the input chain.
		 */
		unsigned short    number_of_nics;

		/*
		 * The number of available CPU cores, as specified by
		 * the user in the input file.
		 */
		unsigned short    rss_cores;
		/*
		 * The number of available hardware queues, as specified by
		 * the user in the input file.
		 */
		unsigned short    rss_queues;
		/*
		 * The number of available CPU sockets, as specified by
		 * the user in the input file.
		 */
		unsigned short    cpu_sockets_no;

		/*
		 * Divide rss_cores with cpu_sockets_no
		 */
		unsigned short    cores_per_socket;

		/*
		 * While allocating CPU cores to pipelines we need a step
		 * counter to traverse our sockets.
		 */
		unsigned short    cores_step;

		/*
		 * Instead of simply attaching FromDPDKDevice elements to different NIC queues (and cores)
		 * Follow the paths of each FromDPDKDevice descriptor and pin elements along these paths.
		 * This might not always increase the throughput of SNF as inter-core communication
		 * might be increased without any reason.
		 */
		bool              rss_aggressive_pinning;

		/*
		 * Extra prefix to be appended to Click objects
		 * in order to replicate them across multiple
		 * queues of the NIC.
		 */
		const std::string queue_ext_prefix = std::string("_q");

		/*
		 * Map [SocketNo] --> [List of core numbers in this socket]
		 */
		std::unordered_map< unsigned short, std::vector <unsigned short> > cpu_layout;

		/*
		 * Flag that denotes wheter we use FastClick or regular Click.
		 */
		bool              fast_click;

	public:
		/*
		 * Public API for RSSGenerator
		 */
		RSSGenerator (Synthesizer *synth);
		~RSSGenerator();

		/*
		 * Implements the abstract method of parent Generator class by calling the
		 * private generate_rss_cloned_pipelines method below.
		 * This method generates a hardware-assisted SNF configuration based
		 * on Click-DPDK and RSS.
		 */
		bool generate_equivalent_configuration(const bool to_file=true);

	private:
		/*
		 * Hardware-assisted, RSS-based SNF:
		 *    RSS-Hashing in the NIC splits traffic at will (based on fields that we specify).
		 *    Then, a Click-DPDK configuration is reading packets from different queues,
		 *    schedules threads from multiple cores on these queues, and clones the chain
		 *    across all of them.
		 */
		bool generate_rss_cloned_pipelines(const bool &to_file);

		/*
		 * Generate some static information about the interfaces' addresses.
		 */
		void generate_static_configuration(
			std::stringstream &config_stream
		);

		/*
		 * Clone the input, read, write, and output parts of a SNF
		 * chain across multiple CPU cores.
		 */
		bool replicate_input_part_of_synthesis(
			std::stringstream                                     &config_stream,
			std::map < unsigned short, std::string >              &nic_to_classifier,
			std::map < std::string,    unsigned short >           &in_nic_desc_to_core,
			std::map < unsigned short, std::vector<std::string> > &nic_to_classifier_repl,
			std::map < std::string,    std::vector<std::string> > &classifier_to_nic_desc,
			std::map < std::string,    unsigned short >           &classifier_to_core
		);
		bool replicate_read_part_of_synthesis(
			std::stringstream                                     &config_stream,
			std::map < unsigned short, std::string >              &nic_to_classifier,
			std::map < unsigned short, std::vector<std::string> > &nic_to_classifier_repl,
			std::map <
				std::string, std::vector<
				std::pair< std::string, unsigned short > >
			>                                                     &classifier_repl_to_rewriter_repl
		);
		bool replicate_write_and_output_part_of_synthesis(
			std::stringstream                                     &config_stream,
			std::map < std::string,    unsigned short >           &out_nic_desc_to_core,
			std::map < 	std::pair< std::string, unsigned short>,
						std::pair< std::string, unsigned short> > &classifier_repl_to_rewriter
		);

		/*
		 * Map a Classifier to the Rewriter objects that will
		 * modify its traffic classes. One classifier has at maximum
		 * as many rewriters as the number of its traffic classes.
		 * However, sometimes traffic classes from different classifiers
		 * might end up to the same rewriter. This is the case for stateful
		 * middlebox operations such as NAPT, where two directions of the
		 * same flow need to pass through the same stateful rewriter.
		 */
		bool construct_classifier_to_rewriter_map(
			std::map < 	std::pair< std::string, unsigned short>,
						std::pair< std::string, unsigned short> > &classifier_repl_to_rewriter
		);

		/*
		 * Given a rewriter, search in the Classifier-Rewriter map to
		 * find the classifier associated to that specific rewriter.
		 */
		std::pair< std::string, unsigned short > from_rewriter_to_classifier(
			const std::map<
				std::pair< std::string, unsigned short>,
				std::pair< std::string, unsigned short>
			>                                             &classifier_repl_to_rewriter,
			const std::string                             &rewriter_name
		);

		/*
		 * When hardware classification is on, we need to schedule multiple
		 * threads (one per core) to the various NIC queues and attach one
		 * Click-DPDK input element (FromDPDKDevice) to each queue.
		 */
		bool assign_nic_queues_to_cores(
			std::map < std::string, unsigned short >           &in_nic_desc_to_core,
			std::map < std::string, std::vector<std::string> > &classifier_to_nic_desc,
			std::stringstream                                  &config_stream,
			const unsigned short                               &nic_no,
			const std::string                                  &ipcl_name
		);

		/*
		 * Call Click's StaticThreadSched element to schedule
		 * Click instances to different CPU cores.
		 */
		bool scheduling(
			std::map < std::string, unsigned short >        &in_nic_desc_to_core,
			std::map < std::string, unsigned short >        &out_nic_desc_to_core,
			std::map < std::string, unsigned short >        &classifier_to_core,
			std::map <
				std::string, std::vector<
				std::pair< std::string, unsigned short > >
			>                                               &classifier_repl_to_rewriter_repl,
			std::stringstream                               &config_stream
		);

		bool static_path_scheduling(
			std::map <std::string, unsigned short>          &instance_to_core,
			std::stringstream                               &config_stream,
			const std::string                               &purpose
		);

		bool stateful_path_scheduling(
			std::map <std::string, unsigned short>          &classifier_to_core,
			std::map <
				std::string, std::vector<
				std::pair< std::string, unsigned short > >
			>                                               &classifier_repl_to_rewriter_repl,
			std::stringstream                               &config_stream
		);

		/*
		 * Based on the input configuration, build a map of cores assigned to
		 * sockets
		 */
		void build_cpu_layout(void);
		void print_cpu_layout(void);

		/*
		 * Ask the CPU layout for the list of cores that belong to the same
		 * socket with the excluded core (which does the I/O).
		 * These cores will do processing.
		 */
		std::vector< unsigned short > get_proc_cores_of_this_socket(
			const unsigned short &core_to_exclude
		);

		/*
		 * A global way of naming NIC descriptors
		 */
		std::string get_nic_desc_for_queue(const unsigned short &nic_no, const std::string &mode);

		/*
		 * Based on the ClickType selected by the user, generate the respective
		 * piece of code for I/O (i.e., Click or FastClick).
		 */
		std::string get_io_classes_by_type(void) const;
};

#endif
