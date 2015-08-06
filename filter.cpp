#include <algorithm>  //std::set_intersection std::sort
#include <iostream> //std::cerr
#include <climits> //UINT_MAX
#include <cstdio>

#include "filter.hpp"
#include "click_element.hpp"
#include "output_class.hpp"

#define for_allowed_values(it) for (auto it=m_allowedValues.begin() ; \
								it != m_allowedValues.end(); ++it)
#define MIN(a,b) (a>b) ? b : a
#define MAX(a,b) (a>b) ? a : b

#define in_range(x,lower,upper) (x>=lower && x<=upper)
#define DEBUG(a) printf("[%s:%d] %s\n",__FILE__,__LINE__,a)

Filter Filter::get_range_filter(uint32_t lower,uint32_t upper) {
	Filter f;
	f.m_lowerLimit=lower;
	f.m_upperLimit=upper;
	return f;
}

Filter Filter::get_equals_filter(uint32_t value) {
	Filter f;
	f.m_upperLimit = f.m_lowerLimit = value;
	f.m_type=Equals;
	return f;
}

Filter Filter::get_filter_from_v4_prefix(uint32_t value, uint32_t prefix) {
	Filter f;
	uint32_t translation = 32 - prefix;
	f.m_lowerLimit = value & (0xffffffff << translation);
	f.m_upperLimit = value | (0xffffffff >> prefix);	
	
	if(f.m_lowerLimit == f.m_upperLimit) {
		f.m_type = Equals;
	}
	return f;
}

//TODO: if output type is None print error message
Filter& Filter::operator += (const Filter &rhs) {
	
	FilterType rhs_type = rhs.m_type;

	switch (rhs_type) {
		case None :
			this->m_type=None;
			break;
		case Equals :
			this->intersect_equals(rhs.m_lowerLimit);
			break;
		case List :
			this->intersect_list(rhs.m_allowedValues);
			break;
		case Range:
			this->intersect_range(rhs.m_lowerLimit, rhs.m_upperLimit);
			break;
	}
	
	return *this;
}

void Filter::intersect_equals (uint32_t value) {
	switch (this->m_type) {
		case None:
			return;
		case Equals :
			if (this->m_lowerLimit != value) {
				this->m_type=None;
			}
			return;
		case List:
			for_allowed_values(it) {
				if (*it == value) {
					this->m_lowerLimit = this->m_upperLimit = value;
					this->m_type = Equals;
					return;
				}
			}
			this->m_type=None;
			return;
		case Range:
			if in_range(value,this->m_lowerLimit, this->m_upperLimit) {
				this->m_lowerLimit = this->m_upperLimit = value;
				this->m_type = Equals;
				return;
			}
			this->m_type=None;
			return;
	}
}

void Filter::intersect_list (const std::vector<uint32_t> &list) {

	switch (this->m_type) {
		case None:
			return;
		case Equals : {
			for (auto it=list.begin() ; it != list.end(); ++it) {
				if (*it == this->m_lowerLimit) { return; }
			}
			this->m_type=None;
			return;
		}
		case List: {
			std::vector<uint32_t> intersection(list.size());
			std::set_intersection (list.begin(), list.end(), 
					this->m_allowedValues.begin(), this->m_allowedValues.end(), 
					intersection.begin() );
			this->m_allowedValues = intersection;
			this->update_type_from_list(intersection);		
			return;
		}
		case Range: {
			std::vector<uint32_t> temp;
			for (auto it=list.begin() ; it != list.end(); ++it) {
				if in_range(*it, this->m_lowerLimit, this->m_upperLimit) {
					temp.push_back(*it);
				}
			}
			this->m_allowedValues = temp;
			
			this->update_type_from_list(temp);
			return;
		}
	}
}

void Filter::intersect_range (uint32_t lower, uint32_t upper) {
	switch (this->m_type) {
		case None:
			return;
		case Equals :
			if (!in_range(this->m_lowerLimit,lower,upper)) {
				this->m_type = None;
			}
			return;
		case List : {
			std::vector<uint32_t> temp;
			for_allowed_values(it) {
				if in_range(*it, this->m_lowerLimit, this->m_upperLimit) {
					temp.push_back(*it);
				}
			}
			this->update_type_from_list(temp);
			return;
		}
		case Range : {
			this->m_lowerLimit = MAX(this->m_lowerLimit, lower);
			this->m_upperLimit = MIN(this->m_upperLimit, upper);
			if ( this->m_lowerLimit > this->m_upperLimit ) {
				this->m_type = None;
			}
			else if (this->m_lowerLimit == this->m_upperLimit) {
				this->m_type=Equals;
			}
			return;
		}
	}
}

void Filter::update_type_from_list(const std::vector<uint32_t>& list) {
	if(list.empty() ) { 
		this->m_type = None; 
	}
	else if (list.size() == 1) {
		this->m_type = Equals;
		this->m_lowerLimit = this->m_upperLimit = list[0];
	} 
	else {
		this->m_type = List;
	}
}

bool Filter::match(uint32_t value) const {
	switch (this->m_type) {
		case None:
			return false;
		case Equals:
			return (value == this->m_lowerLimit);
		case List:
			return (std::any_of(this->m_allowedValues.begin(), 
							this->m_allowedValues.end(), 
							[=](uint32_t i){return (i==value);}));
		case Range:
			return in_range(value,m_lowerLimit,m_upperLimit);
	}
	return false;
}

Filter Filter::translate(int value) const {
	Filter translated_filter = (*this);
	if (value>=0) {
		translated_filter.move_forward((uint32_t) value);
	}
	else {
		translated_filter.move_backward((uint32_t) -value);
	}
	return translated_filter;
}

inline uint32_t add_to_infinity (uint32_t a, uint32_t b) {
	return (((UINT_MAX-a) < b) ? UINT_MAX : a+b);
}

//Does a-b with 0 as a lower limit
inline uint32_t substract_to_zero (uint32_t a, uint32_t b) {
	return ((a>b) ? a-b : 0);
}

void Filter::move_forward(uint32_t value) {
	m_lowerLimit = add_to_infinity(m_lowerLimit, value);
	m_upperLimit = add_to_infinity(m_upperLimit, value);
	for_allowed_values(it) {
		*it = add_to_infinity(*it,value);
	}
}

FilterType Filter::get_type() const {
	return m_type;
}

void Filter::set_type(FilterType type) {
	m_type = type;
}

void Filter::move_backward(uint32_t value) {
	m_lowerLimit = substract_to_zero(m_lowerLimit, value);
	m_upperLimit = substract_to_zero(m_upperLimit, value);
	for_allowed_values(it) {
		*it = substract_to_zero(*it,value);
	}
}

std::string Filter::to_str () {
	std::string output;
	switch (this->m_type) {
		case None:
			output = "None()";
			break;
		case Equals:
			output = "Equals("+std::to_string(m_lowerLimit)+")";
			break;
		case List:
			output = "List(";
			for_allowed_values(it) {
				output += std::to_string(*it);
				output += ",";
			}
			output += ")";
			break;
		case Range:
			output = "Range("+std::to_string(m_lowerLimit)+","+std::to_string(m_upperLimit)+")";
			break;
	}
	return output;
}

int TrafficClass::addElement (std::shared_ptr<ClickElement> element, int port) {

	int nb_none_filters=0;
	(this->m_elementPath).push_back(element);


	if (port==-1) { //Last element of the chain -> no children
		return 0;
	}
	PacketFilter pf = (element->get_outputPorts()[port]).get_filter();
	
	for_fields_in_pf(it,pf) {
		HeaderField field = it->first;
		Filter* filter = &(it->second);
		
		FieldOperation *field_op = this->m_operation.get_field_op(field);
		
		if (field_op) {
			uint32_t field_value = field_op->m_value;
			switch (field_op->m_type) {
				case Write:
					if (!filter->match(field_value)) {
						(this->m_filters[field]).set_type(None);
						nb_none_filters++;
					}
					break;
				case Translate: {
					Filter translated_filter = filter->translate(-field_value);
					this->m_filters[field] += translated_filter;
					if( (this->m_filters[field]).get_type() == None) {
						nb_none_filters++;
					}
					break;
				}
				default:
					std::cerr<<"["<<__FILE__<<":"<<__LINE__<<"] Found non "
							"modifying operation"<<std::endl;
					exit(1);
				
			}
		}
		else {
			this->m_filters[field] += (*filter);
			
			if( (this->m_filters[field]).get_type() == None) {
				nb_none_filters++;
			}
		}
	}

	this->m_operation.compose_op((element->get_outputPorts()[port]).get_operation());
	return nb_none_filters;
}

std::string TrafficClass::to_str() {
	std::string output = "======== Begin Traffic Class ========\nFilters:\n";
	for_fields_in_pf(it,m_filters) {
		output += ("\tField "+headerFieldNames[it->first]+": "+it->second.to_str()+"\n");
	}
	output += m_operation.to_str();
	
	output += "Passed elements: \n\t";
	for (auto it : m_elementPath) {
		output += elementNames[it->get_type()];
		if (it->is_leaf()) {
			output+="\n";
		}
		else {
			output+="->";
		}
	}
	output += "========= End Traffic Class =========\n";
	return output;
}
