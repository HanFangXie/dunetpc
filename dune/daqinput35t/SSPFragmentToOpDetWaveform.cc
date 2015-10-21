// -*- mode: c++; c-basic-offset: 2; -*-
// art includes

#include "messagefacility/MessageLogger/MessageLogger.h"
#include "art/Framework/Principal/Event.h"
#include "cetlib/search_path.h"
#include <fstream>

// lbnecode/daqinput35t includes

#include "SSPFragmentToOpDetWaveform.h"

// larsoft includes

#include "Geometry/Geometry.h"

// lbne-raw-data includes
#include "lbne-raw-data/Overlays/SSPFragment.hh"
#include "lbne-raw-data/Overlays/anlTypes.hh"

// NOvAClockFrequency is in MHz.

std::vector<raw::OpDetWaveform>
DAQToOffline::SSPFragmentToOpDetWaveform(artdaq::Fragments const& rawFragments, const double NOvAClockFrequency, std::map<int,int> theChannelMap)
{
  std::vector<raw::OpDetWaveform> opDetWaveformVector;

  unsigned int numFragments = rawFragments.size();

  for (size_t idx = 0; idx < numFragments; ++idx) {
    const auto& frag(rawFragments[idx]);

    lbne::SSPFragment sspf(frag);

    mf::LogDebug("DAQToOffline") << "\n"
				 << "SSP fragment "     << frag.fragmentID() 
				 << " has total size: " << sspf.hdr_event_size()
				 << " and run number: " << sspf.hdr_run_number()
				 << " with " << sspf.total_adc_values() << " total ADC values"
				 << "\n"
				 << "\n";

    const SSPDAQ::MillisliceHeader* meta=0;
    //get the information from the header
    if(frag.hasMetadata())
    {
	meta = &(frag.metadata<lbne::SSPFragment::Metadata>()->sliceHeader);
            
	mf::LogInfo("DAQToOffline")
	  << "===Slice metadata====" << "\n"
	  << "  Start time         " << meta->startTime << "\n"
	  << "  End time           " << meta->endTime << "\n"
	  << "  Packet length      " << meta->length << "\n"
	  << "  Number of triggers " << meta->nTriggers << "\n"
	  << "=====================";
    }
    else
    {
	mf::LogWarning("DAQToOffline") << "SSP fragment has no metadata associated with it.";
    }

      
    const unsigned int* dataPointer = sspf.dataBegin();

        
    for (unsigned int triggersProcessed = 0;
         (meta==0 || triggersProcessed < meta->nTriggers) && dataPointer < sspf.dataEnd();
         ++triggersProcessed) {
      //
      // The elements of the OpDet Pulse
      //
      unsigned short     OpChannel = -1;       ///< Derived Optical channel
      unsigned long      FirstSample = 0;      ///< first sample time in ticks
      unsigned long      InternalSample = 0;   ///< first sample time in ticks
      double             TimeStamp = 0.0;      ///< first sample time in microseconds
      double             InternalTimeStamp = 0.0;
        

            
      const SSPDAQ::EventHeader* daqHeader=reinterpret_cast<const SSPDAQ::EventHeader*>(dataPointer);
      //get the information from the header
 
      uint32_t peaksum = ((daqHeader->group3 & 0x00FF) >> 16) + daqHeader->peakSumLow;
      if(peaksum & 0x00800000) {
	peaksum |= 0xFF000000;
      }

      // Extract values we need for the data product
      if ( theChannelMap.size() == 0) {
        // No channel map, default to debugging map
        int HardwareChannel = ((daqHeader->group2 & 0x000F) >> 0); // Channel Number
        int SSPNumber       = ((daqHeader->group2 & 0x00F0) >> 4); // Module Number
        OpChannel = 100*SSPNumber + HardwareChannel;
      }
      else if ( theChannelMap.find(daqHeader->group2) != theChannelMap.end() ) {
        OpChannel = theChannelMap[daqHeader->group2];
      }
      else {
        int HardwareChannel = ((daqHeader->group2 & 0x000F) >> 0); // Channel Number
        int SSPNumber       = ((daqHeader->group2 & 0x00F0) >> 4); // Module Number
        mf::LogWarning("DAQToOffline") << "SSP " << SSPNumber << " Channel " << HardwareChannel << " not in the map (OK for uninstrumented channels), skipping." << std::endl;
        continue;
      }
      
      FirstSample = ( ( (unsigned long)daqHeader->timestamp[3] << 48 )
		      + ( (unsigned long)daqHeader->timestamp[2] << 32 )
		      + ( (unsigned long)daqHeader->timestamp[1] << 16 )
		      + ( (unsigned long)daqHeader->timestamp[0] ) );

      InternalSample =  ( ((uint64_t)((uint64_t)daqHeader->intTimestamp[3] << 32))
			  + ((uint64_t)((uint64_t)daqHeader->intTimestamp[2]) << 16)
			  + ((uint64_t)((uint64_t)daqHeader->intTimestamp[1])) );

      TimeStamp = ((double)FirstSample)/NOvAClockFrequency;
      InternalTimeStamp = ((double)InternalSample)/NOvAClockFrequency;

      //if (first_FirstSample < 0) {
      //  mf::LogInfo("DAQToOffline") << "Reset first time stamp to " << first_TimeStamp;
      //  first_FirstSample = FirstSample;
      //  first_TimeStamp = TimeStamp;
      //  first_InternalSample = InternalSample;
      //  first_InternalTimeStamp = InternalTimeStamp;
      // }
            
      mf::LogDebug("DAQToOffline")
	<< "Header:                             " << daqHeader->header   << "\n"
	<< "Length:                             " << daqHeader->length   << "\n"
	<< "Trigger type:                       " << ((daqHeader->group1 & 0xFF00) >> 8) << "\n"
	<< "Status flags:                       " << ((daqHeader->group1 & 0x00F0) >> 4) << "\n"
	<< "Header type:                        " << ((daqHeader->group1 & 0x000F) >> 0) << "\n"
	<< "Trigger ID:                         " << daqHeader->triggerID << "\n"
	<< "Module ID:                          " << ((daqHeader->group2 & 0xFFF0) >> 4) << "\n"
	<< "Channel ID:                         " << ((daqHeader->group2 & 0x000F) >> 0) << "\n"
	<< "External (NOvA) timestamp:          " << FirstSample << " ticks" << "\n"
	<< "                                    " << TimeStamp << " microseconds" << "\n"
	// these first_ ouptuts are a little ill-defined anyway since they are not 
	// reset by run but rather with the job
	//<< "Since first sample this run:        " << FirstSample-first_FirstSample << " ticks" << "\n"
	//<< "                                    " << TimeStamp-first_TimeStamp << " microseconds" << "\n"
	<< "Peak sum:                           " << peaksum << "\n"
	<< "Peak time:                          " << ((daqHeader->group3 & 0xFF00) >> 8) << "\n"
	<< "Prerise:                            " << ((daqHeader->group4 & 0x00FF) << 16) + daqHeader->preriseLow << "\n"
	<< "Integrated sum:                     " << ((unsigned int)(daqHeader->intSumHigh) << 8) + (((unsigned int)(daqHeader->group4) & 0xFF00) >> 8) << "\n"
	<< "Baseline:                           " << daqHeader->baseline << "\n"
	<< "CFD Timestamp interpolation points: " << daqHeader->cfdPoint[0] << " " << daqHeader->cfdPoint[1]
	<< " " << daqHeader->cfdPoint[2] << " " << daqHeader->cfdPoint[3] << "\n"
	<< "Internal interpolation point:       " << daqHeader->intTimestamp[0] << "\n"
	<< "Internal timestamp:                 " << InternalSample << " ticks\n"
	<< "                                    " << InternalTimeStamp << " microseconds" << "\n"
	//<< "Relative internal timestamp:        " << InternalSample-first_InternalSample << " ticks" << "\n"
	//<< "                                    " << InternalTimeStamp-first_InternalTimeStamp << " microseconds"
	<< ""  ;

            
      dataPointer+=sizeof(SSPDAQ::EventHeader)/sizeof(unsigned int);
 


      unsigned int nADC=(daqHeader->length-sizeof(SSPDAQ::EventHeader)/sizeof(unsigned int))*2;
      const unsigned short* adcPointer=reinterpret_cast<const unsigned short*>(dataPointer);

      // Initialize our 
      raw::OpDetWaveform Waveform(TimeStamp, OpChannel, nADC);


      for(size_t idata = 0; idata < nADC; idata++) {
	const unsigned short* adc = adcPointer + idata;
	Waveform.push_back(*adc);
      }
        

      opDetWaveformVector.emplace_back( std::move(Waveform) );
      dataPointer+=nADC/2;
    } // End of loop over triggers
  } // End of loop over fragments (rawFragments)

  return opDetWaveformVector;
}



void DAQToOffline::BuildOpDetChannelMap(std::string fChannelMapFile, std::map<int,int> &theChannelMap)
{
    theChannelMap.clear();

    int onlineChannel;
    int offlineChannel;
    
    std::string fullname;
    cet::search_path sp("FW_SEARCH_PATH");
    sp.find_file(fChannelMapFile, fullname);
    
    if ( fullname.empty() ) {
      mf::LogWarning("DAQToOffline") << "Input spectrum file "
                                        << fChannelMapFile
                                        << " not found in FW_SEARCH_PATH, using debugging map!\n";
    } 
    else {
      mf::LogVerbatim("DAQToOffline") << "Build Online->Offline channel Map from " << fullname;
      std::ifstream infile(fullname);
      while (infile.good()) {
        infile >> onlineChannel >> offlineChannel;
        theChannelMap[onlineChannel] = offlineChannel;
        mf::LogVerbatim("DAQToOffline") << "   " << onlineChannel << " -> " << offlineChannel;
      }
    }
    
}



