//============================================================================
//        Name: properties.hpp
//   Copyright: KTH ICT CoS Network Systems Lab
// Description: Defines generic properties that drive the synthesis.
//              Among them, the output folder and filename where Hyper-NF
//              will generate the results, whether Hyper-NF will produce a 
//              pure software or a hardware assisted synthesis. If the latter
//              is selected, the number of CPU sockets, cores, NIC hardware 
//              queues the type of CPU architecture are requested.          
//============================================================================

#ifndef _PROPERTIES_HPP_
#define _PROPERTIES_HPP_

#include <unordered_map>

// Default folder to save the synthesized Click configuration
#define DEFAULT_HYPER_NF_OUT_FOLDER static_cast<std::string> ("./results/")

// Default filename to save the synthesized Click (and RSS) configuration(s)
#define DEFAULT_HYPER_NF_CONF_NAME static_cast<std::string> ("synth-nf")

// Indicative defaults
#define DEFAULT_CPU_CORES_NO   16
#define DEFAULT_CPU_SOCKETS_NO 2

// Indicative upper-bounds
#define MAX_CPU_CORES_NO       512
#define MAX_CPU_SOCKETS_NO     128
#define MAX_NIC_HW_QUEUES_NO   1024

/*
 * Possible formats of Traffic Classes:
 * |--> To IPClassifier  (Click)
 * |--> To RSS-Hashing   (Intel)
 * |--> To Flow Director (Intel)
 * |--> To OpenFlow
 */
enum TrafficClassFormat {
	Click,
	RSS_Hashing,
	Flow_Director,
	OpenFlow
};

/*
 * Convert the property value given by the user (string) to one of the
 * above types.
 */
const std::unordered_map<std::string, TrafficClassFormat> TCLabelToFormat = {
	{static_cast<std::string> ("Click"),         Click         },
	{static_cast<std::string> ("RSS-Hashing"),   RSS_Hashing   },
	{static_cast<std::string> ("Flow-Director"), Flow_Director },
	{static_cast<std::string> ("OpenFlow"),      OpenFlow      }
};

// Default method to classify the traffic in hardware (NIC).
#define DEFAULT_TC_FORMAT Click

/*
 * Class that groups useful system properties for Hyper-NF.
 */
class Properties {
	private:
		/*
		 * Output folder. This is where Hyper-NF Generator places the output files.
		 */
		std::string output_folder;

		/*
		 * Output filename. This is the file generated by Hyper-NF (in the output_folder).
		 * Note that based on the property file choices, this can be either one .click file
		 * (i.e., output_filename.click) or a set of files (e.g., output_filename.click and
		 * output_filename.rss)
		 */
		std::string output_filename;

		/*
		 * Boolean that indicates whether the final output will target Intel-RSS + Click DPDK
		 * or simply Click.
		 */
		bool hardware_classification;

		/*
		 * If hardware_classification is set, one of the following formats are supported:
		 * |--> RSS-Hashing: Splits traffic based on hash functions on specified fields.
		 *      Then, each core will execute the same (entire) Hyper-NF chain.
		 *      Benefits come form parallel processing.
		 * |--> FlowDirector: Assigns traffic to cores based on concrete Flow Director rules.
		 * |--> OpenFlow: Trasform a traffic class into OpenFlow rules that can be injected
		 *      into a software switch (e.g., OVS). Then OVS will send each traffic class to 
		 *      a different virtual interface.
		 * Otherwise, Click is the standard, all-in-software way.
		 */
		TrafficClassFormat traffic_classification_format;

		/*
		 * Boolean that indicates whether the target system supports
		 * Non-Uniform Memory Access (NUMA).
		 * This helps to do core allocation.
		 */
		 bool numa;

		 /*
		 * How many CPU sockets the target system has.
		 * This helps to do core allocation.
		 */
		 unsigned short cpu_sockets_no;

		 /*
		 * How many CPU cores (in total) the target system has.
		 * This helps to do core allocation.
		 */
		 unsigned short cpu_cores_no;

		 /*
		 * How many hardware queues the target system's NIC has.
		 * This helps to do core allocation per queue.
		 */
		 unsigned short nic_hw_queues_no;

	public:

		/*
		 * Default contructor with indicative properties
		 */
		Properties () {
			this->output_folder   = DEFAULT_HYPER_NF_OUT_FOLDER;
			this->output_filename = DEFAULT_HYPER_NF_CONF_NAME;
			this->numa = true;
			this->hardware_classification = false;
			this->traffic_classification_format = DEFAULT_TC_FORMAT;
			this->cpu_sockets_no   = DEFAULT_CPU_SOCKETS_NO;
			this->cpu_cores_no     = DEFAULT_CPU_CORES_NO;
			// Modern NICs have a lot of queues but it makes sence
			// to adapt this number to the available CPU cores
			this->nic_hw_queues_no = this->cpu_cores_no;
		};

		/*
		 * Contructor with dynamic properties
		 */
		Properties(const std::string& out_fold, const std::string& out_file, const bool hw_class, 
					const TrafficClassFormat tc_format, const bool nm, const unsigned short sockets_no,
					const unsigned short cores_no, const unsigned short nic_queues)
					: output_folder(out_fold), output_filename(out_file), hardware_classification(hw_class), 
					traffic_classification_format(tc_format), numa(nm), cpu_sockets_no(sockets_no),
					cpu_cores_no(cores_no), nic_hw_queues_no(nic_queues)
					{};

		/*
		 * Getters
		 */
		inline std::string get_output_folder       (void)  { return this->output_folder;   }
		inline std::string get_output_filename     (void)  { return this->output_filename; }

		inline bool has_numa                       (void) { return this->numa; }
		inline bool has_hardware_classification    (void) { return this->hardware_classification; }
		inline TrafficClassFormat get_traffic_classification_format(void) {
			return this->traffic_classification_format;
		}

		inline unsigned short get_cpu_cores_no     (void) { return this->cpu_cores_no;     }
		inline unsigned short get_cpu_sockets_no   (void) { return this->cpu_sockets_no;   }
		inline unsigned short get_nic_hw_queues_no (void) { return this->nic_hw_queues_no; }

		/*
		 * Setters (Basic sanity check)
		 */
		inline void set_output_folder           (std::string& out_fold) { this->output_folder   = out_fold; }
		inline void set_output_filename         (std::string& out_file) { this->output_filename = out_file; }

		inline void set_numa                    (bool nm)       { this->numa                    = nm; }
		inline void set_hardware_classification (bool hw_class) { this->hardware_classification = hw_class; }
		inline void set_traffic_classification_format (TrafficClassFormat tc_format) {
			this->traffic_classification_format = tc_format;
		}

		inline void set_cpu_cores_no            (unsigned short cores_no)   {
			assert ( (cores_no > 0) && (cores_no < MAX_CPU_CORES_NO) );
			this->cpu_cores_no = cores_no;
		}
		inline void set_cpu_sockets_no          (unsigned short sockets_no) {
			assert ( (sockets_no > 0) && (sockets_no < MAX_CPU_SOCKETS_NO) );
			this->cpu_sockets_no = sockets_no;
		}
		inline void set_nic_hw_queues_no        (unsigned short nic_queues) {
			assert ( (nic_queues > 0) && (nic_queues < MAX_NIC_HW_QUEUES_NO) );
			if ( nic_queues == 0 ) return;
			this->nic_hw_queues_no = nic_queues;
		}
};

/*
 * Translate the traffic classification enum type to text.
 */
inline const std::string tc_to_label(const TrafficClassFormat tc_format) {
	switch (tc_format) {
		case Click:
			return std::string("Click");
		case RSS_Hashing:
			return std::string("RSS-Hashing");
		case Flow_Director:
			return std::string("Flow-Director");
		case OpenFlow:
			return std::string("OpenFlow");
		default:
			throw std::runtime_error("Unknown Traffic Class format");
	}
}

#endif