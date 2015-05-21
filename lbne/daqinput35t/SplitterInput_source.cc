// art
#include "art/Framework/Core/FileBlock.h"
#include "art/Framework/Core/InputSourceMacros.h"
#include "art/Framework/Core/ProductRegistryHelper.h"
#include "art/Framework/IO/Root/rootNames.h"
#include "art/Framework/IO/Sources/Source.h"
#include "art/Framework/IO/Sources/SourceHelper.h"
#include "art/Framework/IO/Sources/SourceTraits.h"
#include "art/Framework/IO/Sources/put_product_in_principal.h"
#include "art/Persistency/Provenance/EventID.h"
#include "art/Persistency/Provenance/MasterProductRegistry.h"
#include "art/Persistency/Provenance/RunID.h"
#include "art/Persistency/Provenance/SubRunID.h"
#include "art/Utilities/InputTag.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

//#include <iostream>

// artdaq
#include "artdaq-core/Data/Fragments.hh"

// lardata
#include "RawData/RawDigit.h"

// lbne
#include "lbne/daqinput35t/tpcFragmentToRawDigits.h"
#include "lbne/daqinput35t/SSPFragmentToOpDetWaveform.h"

// C++
#include <functional>
#include <memory>
#include <regex>
#include <string>
#include <vector>

// ROOT
#include "TTree.h"
#include "TFile.h"

using raw::RawDigit;
using raw::OpDetWaveform;
using std::vector;
using std::string;

// TODO:
//  Split the SSP data too.
//  Change the index in the loadeddigits based on where we want to draw the split
//  Put in external trigger (Penn board) info when we get it
//  Deal with ZS data -- currently this assumes non-ZS data
//  Discover if an event is not contiguous with the next event and do not stitch in that case -- dump the
//    loaded digits and the partially constructed event and start over
//  Make it capable of splitting MC as well -- already have raw::RawDigit and raw::OpDetWaveform data products
//    in the MC
//  There's an assumption that the channels are the same event to event and they come in the same order --
//    would need a list of channels from one event to another to look it up properly

//==========================================================================

namespace {

  struct LoadedWaveforms {

    LoadedWaveforms() : waveforms(), index() {}

    vector<OpDetWaveform> waveforms;
    size_t index;

    void assign( vector<OpDetWaveform> const & v ) {
      waveforms = v;
      index = 0ul;
    }

    // not really used 
    bool empty() const 
    { 
      if (waveforms.size() == 0) return true;
      return false;
    }

    // provide a method to dig out a particular OpDetWaveform at a particular time

  };


  struct LoadedDigits {

    LoadedDigits() : digits(), index() {}

    vector<RawDigit> digits;
    size_t index;

    void assign( vector<RawDigit> const & v ) {
      digits = v;
      index = 0ul;
    }

    bool empty() const 
    { 
      //std::cout << "Checking if loaded digits is empty" << std::endl;
      //std::cout << "Number of rawdigits: " << digits.size() << std::endl;
      if (digits.size() == 0) return true;
      //std::cout << "Number of samples: " << digits[0].Samples() << std::endl;
      if (digits[0].Samples() == 0) return true;
      if (index >= digits[0].Samples()) return true; 
      return false;
    }

    RawDigit::ADCvector_t next() {
      ++index;
      RawDigit::ADCvector_t digitsatindex;
      for (size_t ichan=0; ichan<digits.size(); ichan++)
	{
	  digitsatindex.push_back(digits[ichan].ADC(index-1));
	}
      return digitsatindex;
    }

  };

  // Retrieves branch name (a la art convention) where object resides
  template <typename PROD>
  const char* getBranchName( art::InputTag const & tag )
  {
    std::ostringstream pat_s;
    pat_s << art::TypeID(typeid(PROD)).friendlyClassName()
          << '_'
          << tag.label()
          << '_'
          << tag.instance()
          << '_'
          << tag.process()
          << ".obj";
    return pat_s.str().data();
  }

  artdaq::Fragments*
  getFragments( TBranch* br, unsigned entry )
  {
    br->GetEntry( entry );
    return reinterpret_cast<artdaq::Fragments*>( br->GetAddress() );
  }

  // Assumed file format is
  //
  //     "lbne_r[digits]_sr[digits]_other_stuff.root"
  //
  // This regex object is used to extract the run and subrun numbers.
  // The '()' groupings correspond to regex matches that can be
  // extracted using the std::regex_match facility.
  std::regex const filename_format( "lbne_r(\\d+)_sr(\\d+).*\\.root" );

}

//==========================================================================

namespace raw {

  // Enable 'pset.get<raw::Compress_t>("compression")'
  void decode (boost::any const & a, Compress_t & result ){
    unsigned tmp;
    fhicl::detail::decode(a,tmp);
    result = static_cast<Compress_t>(tmp);
  }
}

//==========================================================================

namespace DAQToOffline
{
  // The class Splitter is to be used as the template parameter for
  // art::Source<T>. It understands how to read art's ROOT data files,
  // to extract artdaq::Fragments from them, how to convert the
  // artdaq::Fragments into vectors of raw::RawDigit objects, and
  // finally how to re-combine those raw::RawDigit objects to present
  // the user of the art::Source<Splitter> with a sequence of
  // art::Events that have the desired event structure, different from
  // the event structure of the data file(s) being read.

  class Splitter
  {
  public:
    Splitter(fhicl::ParameterSet const& ps,
             art::ProductRegistryHelper& prh,
             art::SourceHelper& sh);

    // See art/Framework/IO/Sources/Source.h for a description of each
    // of the public member functions of Splitter.
    bool readFile(string const& filename, art::FileBlock*& fb);

    bool readNext(art::RunPrincipal* const& inR,
                  art::SubRunPrincipal* const& inSR,
                  art::RunPrincipal*& outR,
                  art::SubRunPrincipal*& outSR,
                  art::EventPrincipal*& outE);

    void closeCurrentFile();

  private:

    using rawDigits_t = vector<RawDigit>;
    using SSPWaveform_t = vector<OpDetWaveform>;

    string                 sourceName_;
    string                 lastFileName_;
    std::unique_ptr<TFile> file_;
    bool                   doneWithFiles_;
    art::InputTag          TPCinputTag_;
    art::InputTag          SSPinputTag_;
    double                 fNOvAClockFrequency; // MHz
    art::SourceHelper      sh_;
    TBranch*               TPCfragmentsBranch_;
    TBranch*               SSPfragmentsBranch_;
    LoadedDigits           loadedDigits_;
    LoadedWaveforms        loadedWaveforms_;
    size_t                 nInputEvts_;
    size_t                 treeIndex_;
    art::RunNumber_t       runNumber_;
    art::SubRunNumber_t    subRunNumber_;
    art::EventNumber_t     eventNumber_;
    art::RunNumber_t       cachedRunNumber_;
    art::SubRunNumber_t    cachedSubRunNumber_;
    size_t                 ticksPerEvent_;
    rawDigits_t            bufferedDigits_;
    std::vector<RawDigit::ADCvector_t>  dbuf_;
    unsigned short         fTicksAccumulated;

    std::function<rawDigits_t(artdaq::Fragments const&)> fragmentsToDigits_;

    bool eventIsFull_(rawDigits_t const & v);

    bool loadDigits_();

    void makeEventAndPutDigits_( art::EventPrincipal*& outE );

  };
}

//=======================================================================================
DAQToOffline::Splitter::Splitter(fhicl::ParameterSet const& ps,
                                 art::ProductRegistryHelper& prh,
                                 art::SourceHelper& sh) :
  sourceName_("SplitterInput"),
  lastFileName_(ps.get<vector<string>>("fileNames",{}).back()),
  file_(),
  doneWithFiles_(false),
  //  TPCinputTag_("daq:TPC:DAQ"), // "moduleLabel:instance:processName"
  TPCinputTag_(ps.get<string>("TPCInputTag")), // "moduleLabel:instance:processName"
  SSPinputTag_(ps.get<string>("SSPInputTag")), // "moduleLabel:instance:processName"
  fNOvAClockFrequency(ps.get<double>("NOvAClockFrequency",64.0)),
  sh_(sh),
  TPCfragmentsBranch_(nullptr),
  SSPfragmentsBranch_(nullptr),
  nInputEvts_(),
  runNumber_(1),      // Defaults in case input filename does not
  subRunNumber_(0),   // follow assumed filename_format above.
  eventNumber_(),
  cachedRunNumber_(-1),
  cachedSubRunNumber_(-1),
  ticksPerEvent_(ps.get<size_t>("ticksPerEvent")),
  bufferedDigits_(),
  dbuf_(),
  fTicksAccumulated(0),
  fragmentsToDigits_( std::bind( DAQToOffline::tpcFragmentToRawDigits,
                                 std::placeholders::_1, // artdaq::Fragments
                                 ps.get<bool>("debug",false),
                                 ps.get<raw::Compress_t>("compression",raw::kNone),
                                 ps.get<unsigned>("zeroThreshold",0) ) )
{
  // Will use same instance name for the outgoing products as for the
  // incoming ones.
  prh.reconstitutes<rawDigits_t,art::InEvent>( sourceName_, TPCinputTag_.instance() );
}

//=======================================================================================
bool
DAQToOffline::Splitter::readFile(string const& filename, art::FileBlock*& fb)
{

  // Run numbers determined based on file name...see comment in
  // anon. namespace above.
  std::smatch matches;
  if ( std::regex_match( filename, matches, filename_format ) ) {
    runNumber_       = std::stoul( matches[1] );
    subRunNumber_    = std::stoul( matches[2] );
  }

  // Get fragments branches
  file_.reset( new TFile(filename.data()) );
  TTree* evtree    = reinterpret_cast<TTree*>(file_->Get(art::rootNames::eventTreeName().c_str()));
  TPCfragmentsBranch_ = evtree->GetBranch( getBranchName<artdaq::Fragments>( TPCinputTag_ ) ); // get branch for TPC input tag
  SSPfragmentsBranch_ = evtree->GetBranch( getBranchName<artdaq::Fragments>( SSPinputTag_ ) ); // get branch for SSP input tag
  nInputEvts_      = static_cast<size_t>( TPCfragmentsBranch_->GetEntries() );
  size_t nevt_ssp  = static_cast<size_t>( SSPfragmentsBranch_->GetEntries() );
  if (nevt_ssp != nInputEvts_) throw cet::exception("35-ton SplitterInput: Different numbers of RCE and SSP input events in file");
  treeIndex_       = 0ul;

  // New fileblock
  fb = new art::FileBlock(art::FileFormatVersion(),filename);
  if ( fb == nullptr ) {
    throw art::Exception(art::errors::FileOpenError)
      << "Unable to open file " << filename << ".\n";
  }

  return true;
}

//=======================================================================================
bool
DAQToOffline::Splitter::readNext(art::RunPrincipal*    const& inR,
                                 art::SubRunPrincipal* const& inSR,
                                 art::RunPrincipal*    & outR,
                                 art::SubRunPrincipal* & outSR,
                                 art::EventPrincipal*  & outE)
{

  if ( doneWithFiles_ ) {
    return false;
  }


  while ( fTicksAccumulated < ticksPerEvent_ ) {


    while (loadedDigits_.empty())
      {
	bool rc = loadDigits_();
	if (!rc)
	  {
	    doneWithFiles_ = (file_->GetName() == lastFileName_);
	    return false;
	  }
      }

    // original code from artists.  Doesn't retry if we still have no digits
    // if ( loadedDigits_.empty() && !loadDigits_() ) {
    //  if ( file_->GetName() != lastFileName_ )
    //    return false;
    //  else {
    //    doneWithFiles_ = true;
    //    break;  // the last event may be short if we cannot stitch with the next
    //  }
    // }

    std::vector<short> nextdigits = loadedDigits_.next(); 
    if (dbuf_.size() == 0)
      {
	RawDigit::ADCvector_t emptyvector; 
	for (size_t ichan=0;ichan<nextdigits.size();ichan++) dbuf_.push_back(emptyvector);
      }
    for (size_t ichan=0;ichan<nextdigits.size();ichan++)
      {
	dbuf_[ichan].push_back(nextdigits[ichan]);
      }
    fTicksAccumulated ++;
  }

  // copy all the info (channel number, digits, pedestal, sigma, compression) from the current loaded digits

  for (size_t ichan=0;ichan<dbuf_.size();ichan++)
    {
      RawDigit d(loadedDigits_.digits[ichan].Channel(),
                 fTicksAccumulated,dbuf_[ichan],
                 loadedDigits_.digits[ichan].Compression());
      d.SetPedestal(loadedDigits_.digits[ichan].GetPedestal(),
                    loadedDigits_.digits[ichan].GetSigma());
      bufferedDigits_.emplace_back(d);
    }

  art::Timestamp ts; // LBNE should decide how to initialize this
  if ( runNumber_ != cachedRunNumber_ ){
    outR = sh_.makeRunPrincipal(runNumber_,ts);
    cachedRunNumber_ = runNumber_;
    eventNumber_ = 0ul;
  }

  if ( subRunNumber_ != cachedSubRunNumber_ ) {
    outSR = sh_.makeSubRunPrincipal(runNumber_,subRunNumber_,ts);
    cachedSubRunNumber_ = subRunNumber_;
    eventNumber_ = 0ul;
  }

  makeEventAndPutDigits_( outE );
  return true;

}

//=======================================================================================
void
DAQToOffline::Splitter::closeCurrentFile()
{
  file_.reset(nullptr);
}

//=======================================================================================
bool
DAQToOffline::Splitter::eventIsFull_( vector<RawDigit> const & v )
{
  return v.size() == ticksPerEvent_;
}

//=======================================================================================
bool
DAQToOffline::Splitter::loadDigits_()
{
  if ( loadedDigits_.empty() && treeIndex_ != nInputEvts_ ) {
    auto* SSPfragments = getFragments( SSPfragmentsBranch_, treeIndex_ );
    auto* fragments = getFragments( TPCfragmentsBranch_, treeIndex_++ );
    rawDigits_t const digits = fragmentsToDigits_( *fragments );
    loadedDigits_.assign( digits );
    std::vector<raw::OpDetWaveform> waveforms = DAQToOffline::SSPFragmentToOpDetWaveform(*SSPfragments, fNOvAClockFrequency);
    loadedWaveforms_.assign( waveforms );
    // todo -- unpack SSP fragments
    return true;
  }
  else return false;
}

//=======================================================================================
void
DAQToOffline::Splitter::makeEventAndPutDigits_(art::EventPrincipal*& outE){
  ++eventNumber_;
  outE = sh_.makeEventPrincipal( runNumber_, subRunNumber_, eventNumber_, art::Timestamp() );
  art::put_product_in_principal( std::make_unique<rawDigits_t>(bufferedDigits_),
                                 *outE,
                                 sourceName_,
                                 TPCinputTag_.instance() );
  mf::LogDebug("DigitsTest") << "Producing event: " << outE->id() << " with " << bufferedDigits_.size() << " digits";
  bufferedDigits_.clear();
  for (size_t ichan=0;ichan<dbuf_.size();ichan++) { dbuf_[ichan].clear(); }
  dbuf_.clear();
  fTicksAccumulated = 0;
}

//=======================================================================================
DEFINE_ART_INPUT_SOURCE(art::Source<DAQToOffline::Splitter>)
//=======================================================================================
