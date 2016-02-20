//============================================================================
//        Name: synthesizer.cpp
//   Copyright: KTH ICT CoS Network Systems Lab
// Description: Compose one Click configuration out of a chain of
//              Click configurations.
//============================================================================

#include "synthesizer.hpp"

#include "../shared/helpers.hpp"

#include "../traffic_class_builder/click_tree.hpp"
#include "../traffic_class_builder/output_class.hpp"
#include "../traffic_class_builder/click_element.hpp"

ConsolidatedTc::ConsolidatedTc () :
	m_nf_of_out_iface(),
	m_out_iface      (),
	m_out_iface_conf (),
	m_operation      (),
	m_pattern        (),
	m_chain          (),
	m_inputPort      (256),
	m_nat            ()
	{}

ConsolidatedTc::ConsolidatedTc (const std::string& nf_of_out_iface, const std::string& out_iface,
								const std::string& out_iface_conf, const std::string& op,
								const std::string& chain) :
	m_nf_of_out_iface(nf_of_out_iface),
	m_out_iface      (out_iface),
	m_out_iface_conf (out_iface_conf),
	m_operation      (op),
	m_chain          (chain),
	m_inputPort      (256) {}

void
ConsolidatedTc::add_tc (const TrafficClass& tc, const TrafficClassFormat& tc_format) {

	switch (tc_format) {

		case Click:
		case RSS_Hashing:
			if( !m_pattern.empty() ) {
				m_pattern += " || ";
			}
			m_pattern += "("+tc.to_ip_classifier_pattern()+")";
			break;

		case Flow_Director:
			throw std::runtime_error("Unimplemented Traffic Class pattern: " + tc_to_label(tc_format));
			//m_pattern = tc.to_flow_director_pattern();
			//break;

		case OpenFlow:
			throw std::runtime_error("Unimplemented Traffic Class pattern: " + tc_to_label(tc_format));
			//m_pattern = tc.to_openflow_pattern();
			//break;

		default:
			throw std::runtime_error("Unimplemented Traffic Class pattern: " + tc_to_label(tc_format));
	}
}

void
ConsolidatedTc::set_nat (std::shared_ptr<SynthesizedNat> nat, unsigned short input_port) {
	this->m_nat = nat->get_name();
	this->m_inputPort = input_port;
}

std::string
ConsolidatedTc::get_chain() {
	return this->m_chain + "[" + std::to_string(this->m_inputPort) + "]" + this->m_nat + ";";
}

Synthesizer::Synthesizer(ChainParser* cp) : tc_per_input_iface(), nat_per_output_iface() {
	this->log.set_logger_file(__FILE__);
	if ( cp == NULL ) {
		throw std::runtime_error("Synthesizer: Invalid Parser object");
	}

	this->parser = cp;
	this->basic_configuration_filename = 
		this->parser->get_chain_graph()->get_properties()->get_output_filename();
	this->soft_configuration_filename = this->basic_configuration_filename + ".click";
	this->hw_classification = 
		this->parser->get_chain_graph()->get_properties()->has_hardware_classification();
	this->traffic_classification_format = 
		 this->parser->get_chain_graph()->get_properties()->get_traffic_classification_format();
	log << debug << "Synthesizer constructed" << def << std::endl;
}

Synthesizer::~Synthesizer() {
	delete this->parser;
	log << debug << "Synthesizer deleted" << def << std::endl;
}

/*
 * Traverse the NF DAGs, jump from NF to NF until you reach an
 * endpoint. Along the way build the traffic classes required for
 * the synthesis.
 */
short
Synthesizer::build_traffic_classes(void) {

	log << "" << std::endl;
	log << info << "==============================================================================" << def << std::endl;
	log << info << "Build Traffic Classes ..." << def << std::endl;

	// The output format of a traffic class must 
	TrafficClassFormat tc_format = this->traffic_classification_format;

	// Get all NFs, one-by-one
	for ( Vertex* v : this->get_chain_parser()->get_chain_graph()->get_chain()->get_vertex_order() ) {
		// The node of this NF in the chain graph
		ChainVertex* cv = static_cast<ChainVertex*>(v);
		unsigned short nf_position = cv->get_position();

		// The Click DAG of this NF
		NFGraph* nf_graph = this->get_chain_parser()->get_nf_graph_at(nf_position);
		if ( nf_graph == NULL )
			return NO_MEM_AVAILABLE;

		log << info << "Network Function: " << cv->get_name() << def << std::endl;

		// Get all the input entry points of this NF (if any)
		// From these points, we start building the traffic classes
		for ( auto& endpoint : nf_graph->get_endpoint_vertices(VertexType::Input) ) {
			std::string elem_name = endpoint->get_name();
			std::string elem_conf = endpoint->get_configuration();
			std::string interface = elem_conf.substr(0, elem_conf.find(","));

			log << "\t" << elem_name << "(" << interface << ")" << def << std::endl;

			// Go DFS until the end of life
			std::shared_ptr<ClickElement> ep(new ClickElement(endpoint));
			TrafficBuilder::traffic_class_builder_dfs(
				this->get_chain_parser()->get_chain_graph()->get_chain(),
				this->get_chain_parser()->get_nf_graphs(),
				nf_position,
				ep,
				interface
			);

			// A chain interface in always internal, hence we keep only the externals;
			// Those are part of the final Hyper-NF configuration.
			if ( ! cv->has_chain_interface(interface) ) {
				this->hyper_nf_ifaces.insert( std::make_pair(cv->get_name(), interface) );
			}

			std::string key = cv->get_name() + "-" + interface;
			ClickTree ct (ep);

			// Create an array with all the traffic classes
			for (auto &tc : ct.get_traffic_classes()) {
				// A valid traffic class
				if ( !tc.is_discarded() ) {
					std::string op_as_str = tc.get_operation().to_iprw_conf();
					//std::cout << op_as_str << std::endl;
					std::string snd_key = op_as_str+"\\"+tc.get_output_iface();
					//std::cout << snd_key << std::endl;
					if( this->tc_per_input_iface[key].find(snd_key) == this->tc_per_input_iface[key].end() ) {
						this->tc_per_input_iface[key][snd_key] = {
							tc.get_nf_of_output_iface(),
							tc.get_output_iface(),
							tc.get_output_iface_conf(),
							op_as_str,
							tc.synthesize_chain()};
					}
					(this->tc_per_input_iface[key][snd_key]).add_tc(tc, tc_format);
				}
			}

			log << "" << def << std::endl;
		}
	}
	log << info << "==============================================================================" << def << std::endl;

	return SUCCESS;
}

/*
 * Builds a path of elements per input-output interface pair
 */
short
Synthesizer::synthesize_nat(void) {
	for (auto &it : this->tc_per_input_iface) {
		for(auto &tc : it.second) {
			// The NF name with the output interface name is the combined key.
			std::string out_nf_and_iface = tc.second.m_nf_of_out_iface + "-" + tc.second.m_out_iface;

			// Create space for the path of elements and the interface's configuration
			if ( this->nat_per_output_iface.find(out_nf_and_iface) == this->nat_per_output_iface.end()) {
				this->nat_per_output_iface[out_nf_and_iface] = std::shared_ptr<SynthesizedNat> ( new SynthesizedNat() );
				this->nat_per_output_iface_conf[out_nf_and_iface] = tc.second.m_out_iface_conf;
			}

			tc.second.set_nat(
				this->nat_per_output_iface[out_nf_and_iface],
				this->nat_per_output_iface[out_nf_and_iface]->add_traffic_class(tc.second, it.first)
			);
		}
	}

	log << "\tSuccessfully synthesized the NF chain" << def << std::endl;

	return SUCCESS;
}

/*
 * Interface-specific methods of the final Hyper-NF chain.
 */
short
Synthesizer::get_hyper_nf_ifaces_no(void) {
	return this->hyper_nf_ifaces.size();
}

void
Synthesizer::print_hyper_nf_ifaces(void) {
	for (auto& iface : this->hyper_nf_ifaces ) {
		log << debug << "[Network Function: " << iface.first << ", Iface: " << iface.second << "]" << def << std::endl;
	}
}

/*
 * Generate a new configuration that depends on the user input.
 * |--> Hardware-assisted, multi-core Hyper-NF
 * If hardware classification is requested, generate two files: (a) an implementation of 
 * the traffic classification in the NIC's language (Intel-RSS), and (b) a Click-DPDK configuration
 * that schedules a multitude of threads (one per core) that handle different pieces of the header space.
 * |--> All-in-Software Hyper-NF
 * If hardware classification is not requested, generate a Click equivalent of the entire chain.
 */
short
Synthesizer::generate_equivalent_configuration(bool to_file) {

	//log << "" << std::endl;
	//log << info << "==============================================================================" << def << std::endl;
	//log << info << "Hyper-NF Generator ..." << def << std::endl;

	TrafficClassFormat tc_format = this->traffic_classification_format;

	if ( this->hw_classification ) {
		switch (tc_format) {
			case RSS_Hashing:
				return this->generate_rss_cloned_pipelines(to_file);
			case Flow_Director:
				// return this->generate_flow_director_split_pipelines(to_file);
			case OpenFlow:
				// return this->generate_openflow_split_pipelines(to_file);
			default:
				throw std::runtime_error("Unimplemented Traffic Class Format: " + tc_to_label(tc_format));
		}
	}
	// Pure software based implementation in Click.
	else {
		return this->generate_all_in_soft_configuration(to_file);
	}

	//log << info << "==============================================================================" << def << std::endl;
}

/*
 * Generate a new Click configuration that implements the chain in one Click module.
 * The configuration must achieve equivalent functionality with the initial one.
 */
short
Synthesizer::generate_all_in_soft_configuration(bool to_file) {

	std::ofstream  *out_file = NULL;
	std::streambuf *def_cout = NULL;

	log << "\tAll-in-Software Hyper-NF Generator..." << def << std::endl;

	//this->print_hyper_nf_ifaces();

	// The generated Click configuration will be written to a file
	if ( to_file ) {
		// Open the output file to host our synthesized chain
		out_file = new std::ofstream(this->soft_configuration_filename);
		// Save old cout buffer and redirect cout to the file above.
		def_cout = std::cout.rdbuf(out_file->rdbuf());
	}

	// Costruct the path of Click elements that will lead this modified
	// (i.e., rewritten by Click IPRewriter already) traffic class out of Click.
	// This set of elements follows IPRewriter.
	for (auto &it : this->nat_per_output_iface) {

		std::string out_nf_and_iface = it.first;
		auto nat = it.second;
		std::string iface_config = out_nf_and_iface + ", " +
									this->nat_per_output_iface_conf[out_nf_and_iface];

		std::cout 	<< "/** NAT going to: " << out_nf_and_iface << " **/\n";	
		std::cout 	<< nat->get_name() << " :: IPRewriter(" << nat->compute_conf() 
					<< "DEC_IP_TTL true, CALC_CHECKSUM true);\n";
		
		std::cout 	<< nat->get_name() << "[" << nat->get_outbound_port()
					<< "] -> EtherEncap(0x0800, $srcMAC, $dstMAC) -> Queue ($queueSize) -> ToDevice ("
					<< iface_config << ");\n";
		
		// Discarded paths
		for(unsigned short i=0; i<nat->get_outbound_port(); i++) {
			std::cout<<nat->get_name()<<"["<<i<<"] -> Discard ();\n";
		}

		std::cout<<"\n";
	}

	int i = 0;

	// Construct the IPClassifier(s) and the path of Click elements
	// that lead to all its/their traffic classes.
	for (auto &it : this->tc_per_input_iface) {
		std::string ipc_name = "ipc"+std::to_string(i++);
		std::cout << ipc_name + " :: IPClassifier (\n";
		std::vector<std::string> chains;
		for (auto &tc: it.second) {
			std::cout << "\t" << tc.second.m_pattern << ",\n";
			chains.push_back(tc.second.get_chain());
		}
		std::cout << ");\n";

		for (size_t i = 0; i<chains.size(); i++) {
			std::cout << ipc_name + "[" << i << "] -> " << chains[i] << "\n";
		}
		std::cout << "FromDevice (" << it.first << ") -> Strip(14) -> MarkIPHeader() -> " + ipc_name + ";\n";
		i++;
	}

	// Reset to standard output again
	if ( to_file ) {
		std::cout.rdbuf(def_cout);
		out_file->close();
		delete out_file;
	}

	log << "\tSuccessfully generated the NF chain synthesis to: \n" << 
		"\t\t\t\t\t\t\t|--> " << this->soft_configuration_filename << def << std::endl;

	return SUCCESS;
}

/*
 * Hardware-assisted, multi-core Hyper-NF has 3 versions:
 * (A) RSS-Hashing in the NIC splits traffic at will (based on fields that we specify).
 *     Then, a Click-DPDK configuration is reading packets from different queues, schedules threads
 *     from multiple cores on these queues, and clones the chain across all of them.
 * (B) Flow Director commands are generated to split the traffic based on specific field values.
 *     This provides a very limited classification but still can share traffic among cores.
 *     Each Click-DPDK configuration implements a subset of the chain.
 * (C) OpenFlow rules are generated to spit the traffic classes to the TCAM.
 *     OpenVSwitch is used to implement these rules and then assign the traffic classes to 
 *     different Click-DPDK processes. This is a split model again, the pipeline is not cloned.
 * If hardware classification is requested, generate two files: (a) an implementation of 
 * the traffic classification in the NIC's language (Intel-RSS), and (b) a Click-DPDK configuration
 * that schedules a multitude of threads (one per core) that handle different pieces of the header space.
 */
short
Synthesizer::generate_rss_cloned_pipelines(bool to_file) {

	std::ofstream *soft_out_file = NULL;
	//std::ofstream **hard_out_file = NULL;
	std::streambuf *def_cout = NULL;
	std::string all_out_files;

	log << "\tHardware-assisted, multi-core Hyper-NF Generator..." << def << std::endl;

	//this->print_hyper_nf_ifaces();

	// The generated Click configuration will be written to a file
	if ( to_file ) {

		// Output file to host our synthesized Click-DPDK chain
		soft_out_file = new std::ofstream(this->soft_configuration_filename);
		all_out_files = "\t\t\t\t\t\t\t|--> " + this->soft_configuration_filename + "\n";

		// Output files (one per interface) to host the RSS commands of each Hyper-NF interface.
		/*hard_out_file = new std::ofstream*[nic_classifiers_no];
		unsigned short i = 0;
		for (auto &it : this->hyper_nf_ifaces) {
			std::string nf = it.first;
			std::string iface = it.second;

			std::pair<std::string, std::string> comb_key = std::make_pair(nf, iface);
			this->hrdw_configuration_per_nic[comb_key] = 
				this->basic_configuration_filename + "_" + nf + "_" + iface + ".rss";
			//std::cout << this->hrdw_configuration_per_nic[comb_key] << std::endl;

			hard_out_file[i] = new std::ofstream(this->hrdw_configuration_per_nic[comb_key]);
			all_out_files += "\t\t\t\t\t\t\t|--> " + this->hrdw_configuration_per_nic[comb_key] + "\n";
			i++;
		}*/

		// Save old cout buffer and redirect cout to the file above.
		def_cout = std::cout.rdbuf(soft_out_file->rdbuf());
	}

	// Step 1: Write some static information about the interfaces'
	// addressing of the Hyper-NF configuration.
	for (unsigned short i = 0 ; i < this->get_hyper_nf_ifaces_no() ; i++) {
		std::cout << "AddressInfo(dev" << i << " $macAddr" << i << " $ipAddr"<< i << ");\n"; 
	}
	std::cout<<"\n";

	// Step 2: Costruct the path of Click elements that will lead each modified
	// (i.e., rewritten by Click IPRewriter already) traffic class out of Click.
	// This path of elements follows IPRewriter.
	for (auto &it : this->nat_per_output_iface) {

		std::string out_nf_and_iface = it.first;
		auto nat = it.second;

		// Be careful, some interfaces that appear here are internal.
		// These need to be discarded.

		std::cout 	<< "/** NAT going to: " << out_nf_and_iface << " **/\n";
		std::cout 	<< nat->get_name() << " :: IPRewriter(" << nat->compute_conf() 
					<< "DEC_IP_TTL true, CALC_CHECKSUM true);\n";
		std::cout 	<< nat->get_name() << "[" << nat->get_outbound_port()
					<< "] -> EtherEncap(0x0800, $srcMAC, $dstMAC) -> ToDPDKDevice ("
					<< out_nf_and_iface << ");\n";

		// Discarded paths
		for(unsigned short i=0; i<nat->get_outbound_port(); i++) {
			std::cout<<nat->get_name()<<"["<<i<<"] -> Discard ();\n";
		}

		std::cout<<"\n";
	}

	unsigned short i = 0;
	// A map between a nic queue's descriptor and a CPU core to handle its packets.
	std::map <std::string, unsigned short> nic_desc_to_core;
	std::map < std::string, std::vector<std::string> > nic_desc_to_ip_class;
	std::string code_buffer("");

	// Step 3: Construct the IPClassifier(s) and the path of Click elements
	// that lead to all its/their traffic classes.
	// Replicate this set of elements across all hardware queues and assign queues to cores.
	for (auto &it : this->tc_per_input_iface) {
		std::string ipc_name = "ipc"+std::to_string(i);
		std::cout << ipc_name + " :: IPClassifier (\n";
		std::vector<std::string> chains;
		for (auto &tc: it.second) {
			std::cout << "\t" << tc.second.m_pattern << ",\n";
			chains.push_back(tc.second.get_chain());
		}
		std::cout<<");\n";
		for (size_t i = 0; i<chains.size(); i++) {
			std::cout << ipc_name + "[" << i << "] -> " << chains[i] << "\n";
		}
		std::cout << std::endl;

		// Multiple FromDPDKDevice
		if ( !this->allocate_queues_to_cores(
				nic_desc_to_core, nic_desc_to_ip_class,
				code_buffer, it.first, i, ipc_name) ) {
			return FAILURE;
		}
		i++;
	}

	// Print the current code
	std::cout << code_buffer;
	code_buffer = "";

	if ( !this->schedule_core_threads_on_queues(nic_desc_to_core, code_buffer) ) {
		return FAILURE;
	}

	// Print again
	std::cout << code_buffer;

	// Form the paths between NIC descriptors and IPClassifiers
	for (auto& it : nic_desc_to_ip_class) {
		// Implode the vector into a large string
		for (auto fd : it.second) {
			std::cout << fd << " -> Strip(14) -> MarkIPHeader() -> " << it.first << ";\n";	
		}
		std::cout << std::endl;	
	}

	// 
	//if ( !this->generate_rss_configuration(hard_out_file, nic_classifiers_no, def_cout) ) {
	//	return FAILURE;
	//}

	// Reset to standard output again
	if ( to_file ) {
		std::cout.rdbuf(def_cout);
		soft_out_file->close();
		delete soft_out_file;
		soft_out_file = NULL;

		//for (unsigned short i = 0 ; i < this->get_hyper_nf_ifaces_no() ; i++) {
		//	hard_out_file[i]->close();
		//	delete hard_out_file[i];
		//}
		//delete[] hard_out_file;
		//hard_out_file = NULL;
	}

	log << "\tSuccessfully generated the NF chain synthesis to: \n" << all_out_files << def << std::endl;

	return SUCCESS;
}

short
Synthesizer::generate_flow_director_split_pipelines(bool to_file) {
	if ( to_file ) {
		// Open the output files to host our synthesized chain
		//soft_out_file = new std::ofstream(this->soft_configuration_filename);
		// One per nic
		// this->hrdw_configuration_per_nic
		//hard_out_file[i] = new std::ofstream(this->hrdw_configuration_per_nic + nic.rss);

		// Save old cout buffer and redirect cout to the file above.
		//def_cout = std::cout.rdbuf(soft_out_file->rdbuf());
	}

	// ....

	// Reset to standard output again
	if ( to_file ) {
		//std::cout.rdbuf(def_cout);
		//soft_out_file->close();
		//hard_out_file->close();
		//delete soft_out_file;
		//delete hard_out_file;
	}

	log << error << "Flow Director traffic class generator not implemented" << def << std::endl;
	return SUCCESS;
}

short
Synthesizer::generate_openflow_split_pipelines (bool to_file) {
	if ( to_file ) {
		// Open the output files to host our synthesized chain
		//soft_out_file = new std::ofstream(this->soft_configuration_filename);
		// One per nic
		// this->hrdw_configuration_per_nic
		//hard_out_file[i] = new std::ofstream(this->hrdw_configuration_per_nic + nic.rss);

		// Save old cout buffer and redirect cout to the file above.
		//def_cout = std::cout.rdbuf(soft_out_file->rdbuf());
	}

	// ....

	// Reset to standard output again
	if ( to_file ) {
		//std::cout.rdbuf(def_cout);
		//soft_out_file->close();
		//hard_out_file->close();
		//delete soft_out_file;
		//delete hard_out_file;
	}

	log << error << "OpenFlow traffic class generator not implemented" << def << std::endl;
	return SUCCESS;
}

/*
 * When hardware classification is on, we need to schedule multiple
 * threads (one per core) to the various NIC queues and attach one 
 * Click-DPDK Input element (FromDPDKDevice) to each queue.
 * The first argument (map of nic descriptors to cores) is passed by reference,
 * updated by the method, and returns back to the caller.
 * After all NICs are iterated, this map will be used by Click's scheduler.
 */
bool
Synthesizer::allocate_queues_to_cores(
		std::map <std::string, unsigned short> &nic_desc_to_core,
		std::map < std::string, std::vector<std::string>>& nic_desc_to_ip_class,
		std::string& code_buffer, const std::string& nf_and_iface_name,
		const unsigned short nic_no, const std::string& ipcl_name) {

	unsigned short i = 0, core_no = 0;
	unsigned short rss_queues   = this->parser->get_chain_graph()->get_properties()->get_nic_hw_queues_no();
	unsigned short rss_cores    = this->parser->get_chain_graph()->get_properties()->get_cpu_cores_no();
	//unsigned short socket_cores = rss_cores / this->parser->get_chain_graph()->get_properties()->get_cpu_sockets_no();

	// NIC 0 -> Socket 0, NIC 1 -> Socket 1, NIC 2 -> Socket 0, ...
	bool even_cores = ( nic_no%2 == 0 )? true:false;

	std::string input_nic_desc = "fd_nic_" + std::to_string(nic_no) +"q_";

	// We start from the appropriate core (and socket)
	core_no = (even_cores) ? 0:1;

	std::vector<std::string> nic_descs;

	// For each queue of this NIC
	for (i=0 ; i<rss_queues ; i++) {
		// Define a Click-DPDK input element per queue per NIC
		code_buffer += input_nic_desc + std::to_string(i) + " :: FromDPDKDevice ("
					+ nf_and_iface_name + ", " + std::to_string(i) + ");\n";

		// Append the vector of descriptors that will be assigned to this IPClassifier
		nic_descs.push_back(input_nic_desc + std::to_string(i));

		// Round-robin of socket's core across all NIC queues.
		nic_desc_to_core[input_nic_desc + std::to_string(i)] = core_no % rss_cores;
		core_no += 2;
	}
	code_buffer += "\n";

	nic_desc_to_ip_class[ipcl_name] = nic_descs;

	return true;
}

/*
 * Compose the arguments of Click's StaticThreadSched element.
 * These arguments are maps of nic descriptors to CPU cores.
 */
bool
Synthesizer::schedule_core_threads_on_queues(std::map <std::string, unsigned short>& nic_desc_to_core, 
		std::string& code_buffer) {

	unsigned short i = 0;

	std::cout << "StaticThreadSched(";
	for (auto& it : nic_desc_to_core) {
		if ( i == nic_desc_to_core.size()-1 ) {
			code_buffer += it.first + " " + std::to_string(it.second) + ");";	
		}
		else {
			code_buffer += it.first + " " + std::to_string(it.second) + ", ";
		}
		i++;
	}
	code_buffer +=  "\n\n";

	return true;
}

/*
 * Dump ethtool configuration for the NICs involved in a Hyper-NF.
 */
bool
Synthesizer::generate_rss_configuration(std::ofstream **hw_out_file,
										unsigned short nics_no,
										std::streambuf* def_cout) {

	for (unsigned short i = 0 ; i < nics_no ; i++) {
		// Move cout to the files where we write the hardware configuration
		def_cout = std::cout.rdbuf(hw_out_file[i]->rdbuf());
		//std::cout << "OOOOOOOOOOOOOO" << std::endl;
	}

	(void)def_cout;
	return true;
}

/*
 * Recursive DFS function to visit all vertices from 'vertex'.
 * The vertices can also belong to different graph, so in reality,
 * this is a recursive graph composition function.
 */
void
TrafficBuilder::traffic_class_builder_dfs(
	Graph* graph,
	NF_Map<NFGraph*> nf_chain,
	unsigned short nf_position,
	std::shared_ptr<ClickElement> elem,
	std::string nf_iface) {

	Logger log(__FILE__);
	ElementVertex* nf_vertex = elem->get_ev();

	// Retrieve the appropriate adjacency list
	Graph::AdjacencyList adjacency_list = nf_chain[nf_position]->get_adjacency_list();

	// We reached an Output vertex and need to find for a connection with a following NF
	if ( adjacency_list.at(nf_vertex).size() == 0 ) {
		// We are looking for an endpoint Outpout element with different configuration (aka interface)
		// Otherwise a loop will be created
		if ( (nf_vertex->is_endpoint()) && (nf_vertex->get_interface() != nf_iface) ) {
			log << "\t\t ----->  ENDPOINT " << nf_vertex->get_name() << "(" << nf_vertex->get_interface() << ")" << def << std::endl;
			return;
		}
		// A way to continue in the chain
		else if ( (!nf_vertex->is_endpoint()) && (nf_vertex->get_interface() != nf_iface) ) {
			// Give me the 'good' paths
			if ( (nf_vertex->get_class() != "Discard") ) {
				unsigned short next_nf_position = nf_vertex->get_glue_nf_position();
				std::string next_nf_iface = nf_vertex->get_glue_iface();

				log << "\t\t -----> JUMP FROM " << nf_vertex->get_name() << "(" << nf_vertex->get_interface() << ")" << def << std::endl;
				//log << "\t\t -----> Next Pos: " << next_nf_position << " at iface " << next_nf_iface << def << std::endl;

				// Change context, move to next graph
				// 1. Change adjacency list
				adjacency_list = nf_chain[next_nf_position]->get_adjacency_list();

				// 2. Change vertex pointer to the first element of the next NF
				bool found = false;
				for ( ElementVertex* input_elem : nf_chain[next_nf_position]->get_vertices_by_stage(VertexType::Input) ) {
					if ( input_elem->get_interface() == next_nf_iface ) {
						nf_vertex = input_elem;
						found = true;
						log << "\t\t ----->        TO " << nf_vertex->get_class() <<  "(" << nf_vertex->get_interface() << ")" << def << std::endl;
						break;
					}
				}

				if ( !found ) {
					log << error << "\t\t Unable to find next jump " << def << std::endl;
				}

				// 3. Change origin interface using the interface of the new vertex
				nf_iface = next_nf_iface;

				// 4. Change NF postion in the Chain DAG
				nf_position = next_nf_position;
			}
			// A path that leads to the cliff
			else {
				log << "\t\t ----->      DROP " << nf_vertex->get_name() << "(" << nf_vertex->get_interface() << ")" << def << std::endl;
			}
		}
		// Do not chain because a loop will be created
		else if ( nf_vertex->get_interface() == nf_iface ) {
			log << "\t\t ----->      LOOP " << nf_vertex->get_name() << "(" << nf_vertex->get_interface() << ")" << def << std::endl;
			return;
		}
		else {
			log << "\t\t ----->       BUG " << nf_vertex->get_name() << "(" << nf_vertex->get_interface() << ")" << def << std::endl;
			return;
		}
	}

	int count = 0;
	for ( auto& neighbour : adjacency_list.at(nf_vertex) ) {

		// A way to get an IPMapper's patterns when you encounter IPRewriter
		/*ElementVertex* ev = (ElementVertex*) neighbour.second;
		if ( ev->get_name() == std::string("IPRewriter") ) {
			log << warn << "\t\tFound: " << ev->get_name() << def << std::endl;
			log << warn << "\t\t\t with conf: " << ev->get_configuration() << def << std::endl;
			for (auto& pair : *(ev->get_implicit_configuration()) ) {
				log << warn << "\t\t\t Port: " << pair.first << " maps to patterns: " << def << std::endl;
				for (auto& pattern : pair.second ) {
					log << warn << "\t\t\t    " << pattern << def << std::endl;
				}
			}
		}*/

		std::shared_ptr<ClickElement> child(
			new ClickElement( static_cast<ElementVertex*> (neighbour.second), neighbour.first )
		);
		child->set_nfName( static_cast<ChainVertex*> (graph->get_vertex_by_position(nf_position))->get_name() );
		elem->set_child(child, count++);
		//log<< "\t\tCreated: "+child->to_str()<<std::endl;
		//log << "\t\t Child: " << ev->get_name() << def << std::endl;

		// Unvisited node --> recursion
		traffic_class_builder_dfs(graph, nf_chain, nf_position, child, nf_iface);
	}
}

void
Synthesizer::test_traffic_class_builder(void) {
	setvbuf(stdout, NULL, _IONBF, 0);

	std::string routing_table = "10/8 0,192.168.5/24 1,0/0 2";
	std::shared_ptr<ClickElement> lookup (new ClickElement("RadixIPLookup",routing_table));

	std::string empty;
	std::shared_ptr<ClickElement> discard (new ClickElement("Discard", empty));

	std::shared_ptr<ClickElement> ttl (new ClickElement("DecIPTTL", empty));

	std::string address = "192.10.0.1";
	std::shared_ptr<ClickElement> fixip (new ClickElement("FixIPSrc", address ));

	//std::string rewrite = "- - 192.168.0.1 100-200# 0 1";
	//std::shared_ptr<ClickElement> iprewriter(new ClickElement(IPRewriter, rewrite));

	std::string filter = "allow src port 100-150";
	std::shared_ptr<ClickElement> ipfilter(new ClickElement("IPFilter", filter));

	//fixip->set_child(iprewriter,0);
	//iprewriter->set_child(ipfilter,0);
	fixip->set_child(ipfilter,0);
	ipfilter->set_child(ttl,0);
	lookup->set_child(ttl,2);
	ttl->set_child(discard,0);
	ClickTree tree(fixip);

	for (auto &it : tree.get_traffic_classes()) {
		std::cout<<it.to_str();
	}
}