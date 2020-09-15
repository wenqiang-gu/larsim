////////////////////////////////////////////////////////////////////////
// Class:       ISCalcSeparate
// Plugin Type: algorithm
// File:        ISCalcSeparate.h and ISCalcSeparate.cxx
// Description:
// Interface to algorithm class for a specific calculation of ionization electrons and scintillation photons
// assuming there is no correlation between the two
// Input: 'sim::SimEnergyDeposit'
// Output: num of Photons and Electrons
// Sept.16 by Mu Wei
////////////////////////////////////////////////////////////////////////

#include "larsim/IonizationScintillation/ISCalcSeparate.h"
#include "larcore/CoreUtils/ServiceUtil.h"

namespace {
  constexpr double scint_yield_factor{1.}; // default
}

namespace larg4 {
  //----------------------------------------------------------------------------
  ISCalcSeparate::ISCalcSeparate()
  {
    fSCE = lar::providerFrom<spacecharge::SpaceChargeService>();
    fLArProp = lar::providerFrom<detinfo::LArPropertiesService>();

    // the recombination coefficient is in g/(MeVcm^2), but we report
    // energy depositions in MeV/cm, need to divide Recombk from the
    // LArG4Parameters service by the density of the argon we got
    // above; this is done in 'CalcIon' function below.
    art::ServiceHandle<sim::LArG4Parameters const> LArG4PropHandle;
    fRecombA = LArG4PropHandle->RecombA();
    fRecombk = LArG4PropHandle->Recombk();
    fModBoxA = LArG4PropHandle->ModBoxA();
    fModBoxB = LArG4PropHandle->ModBoxB();
    fUseModBoxRecomb = (bool)LArG4PropHandle->UseModBoxRecomb();
    fGeVToElectrons = LArG4PropHandle->GeVToElectrons();
  }

  //----------------------------------------------------------------------------
  // fNumIonElectrons returns a value that is not corrected for life time effects
  double
  ISCalcSeparate::CalcIon(detinfo::DetectorPropertiesData const& detProp,
                          sim::SimEnergyDeposit const& edep)
  {
    float e = edep.Energy();
    float ds = edep.StepLength();

    double recomb = 0.;
    double dEdx = (ds <= 0.0) ? 0.0 : e / ds;
    double EFieldStep = EFieldAtStep(detProp.Efield(), edep);

    // Guard against spurious values of dE/dx. Note: assumes density of LAr
    if (dEdx < 1.) { dEdx = 1.; }

    if (fUseModBoxRecomb) {
      if (ds > 0) {
        double const scaled_modboxb = fModBoxB / detProp.Density(detProp.Temperature());
        double const Xi = scaled_modboxb * dEdx / EFieldStep;
        recomb = log(fModBoxA + Xi) / Xi;
      }
      else {
        recomb = 0;
      }
    }
    else {
      double const scaled_recombk = fRecombk / detProp.Density(detProp.Temperature());
      recomb = fRecombA / (1. + dEdx * scaled_recombk / EFieldStep);
    }

    /// Print a recomb as a fuction of Y-X at Z=348 cm
    for (int i=-360; i<360; i++) {
      for (int j=0; j<600; j++) {
          float Z = 348; // cm
          float X = i;
          float Y = j;

          double efield = detProp.Efield(); 

          geo::Point_t tmp(X, Y, Z);
          auto const eFieldOffsets = fSCE->GetEfieldOffsets(tmp);
          double EFieldStep = std::hypot( efield + efield * eFieldOffsets.X(), efield * eFieldOffsets.Y(), efield * eFieldOffsets.Z());
          dEdx = 2.5;
          double const scaled_modboxb = fModBoxB / detProp.Density(detProp.Temperature());
          double const Xi = scaled_modboxb * dEdx / EFieldStep;
          recomb = log(fModBoxA + Xi) / Xi;
          std::cout << X << " " << Y << " " << recomb << std::endl;
      }
    }
    // end print

    // 1.e-3 converts fEnergyDeposit to GeV
    auto const numIonElectrons = fGeVToElectrons * 1.e-3 * e * recomb;

    MF_LOG_DEBUG("ISCalcSeparate")
      << " Electrons produced for " << edep.Energy() << " MeV deposited with " << recomb
      << " recombination: " << numIonElectrons << std::endl;
    return numIonElectrons;
  }

  //----------------------------------------------------------------------------
  std::pair<double, double>
  ISCalcSeparate::CalcScint(sim::SimEnergyDeposit const& edep)
  {
    float const e = edep.Energy();
    int const pdg = edep.PdgCode();

    double numScintPhotons{};
    double scintYield = fLArProp->ScintYield(true);
    if (fLArProp->ScintByParticleType()) {
      MF_LOG_DEBUG("ISCalcSeparate") << "scintillating by particle type";

      switch (pdg) {
      case 2212: scintYield = fLArProp->ProtonScintYield(true); break;
      case 13:
      case -13: scintYield = fLArProp->MuonScintYield(true); break;
      case 211:
      case -211: scintYield = fLArProp->PionScintYield(true); break;
      case 321:
      case -321: scintYield = fLArProp->KaonScintYield(true); break;
      case 1000020040: scintYield = fLArProp->AlphaScintYield(true); break;
      case 11:
      case -11:
      case 22: scintYield = fLArProp->ElectronScintYield(true); break;
      default: scintYield = fLArProp->ElectronScintYield(true);
      }

      numScintPhotons = scintYield * e;
    }
    else {
      numScintPhotons = scint_yield_factor * scintYield * e;
    }

    return {numScintPhotons, GetScintYieldRatio(edep)};
  }

  //----------------------------------------------------------------------------
  ISCalcData
  ISCalcSeparate::CalcIonAndScint(detinfo::DetectorPropertiesData const& detProp,
                                  sim::SimEnergyDeposit const& edep)
  {
    auto const numElectrons = CalcIon(detProp, edep);
    auto const [numPhotons, scintYieldRatio] = CalcScint(edep);
    return {edep.Energy(), numElectrons, numPhotons, scintYieldRatio};
  }
  //----------------------------------------------------------------------------
  double
  ISCalcSeparate::EFieldAtStep(double efield, sim::SimEnergyDeposit const& edep)
  {
    if (not fSCE->EnableSimEfieldSCE()) { return efield; }

    auto const eFieldOffsets = fSCE->GetEfieldOffsets(edep.MidPoint());
    return std::hypot(
      efield + efield * eFieldOffsets.X(), efield * eFieldOffsets.Y(), efield * eFieldOffsets.Z());
  }
  //----------------------------------------------------------------------------
  double
  ISCalcSeparate::GetScintYieldRatio(sim::SimEnergyDeposit const& edep)
  {
    if (!fLArProp->ScintByParticleType()) { return fLArProp->ScintYieldRatio(); }
    switch (edep.PdgCode()) {
    case 2212: return fLArProp->ProtonScintYieldRatio();
    case 13:
    case -13: return fLArProp->MuonScintYieldRatio();
    case 211:
    case -211: return fLArProp->PionScintYieldRatio();
    case 321:
    case -321: return fLArProp->KaonScintYieldRatio();
    case 1000020040: return fLArProp->AlphaScintYieldRatio();
    case 11:
    case -11:
    case 22: return fLArProp->ElectronScintYieldRatio();
    default: return fLArProp->ElectronScintYieldRatio();
    }
  }

} // namespace
