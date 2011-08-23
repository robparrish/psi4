#include "dcft.h"
#include <libdpd/dpd.h>
#include <libtrans/integraltransform.h>
#include <libdiis/diismanager.h>
#include "defines.h"

using namespace boost;

namespace psi{ namespace dcft{

/**
 * Computes the DCFT density matrix and energy
 */
double
DCFTSolver::compute_energy()
{
    bool scfDone    = false;
    bool lambdaDone = false;
    bool densityConverged = false;
    bool energyConverged = false;
#if !ENERGY_NEW
    double oldEnergy;
#endif
    scf_guess();
    mp2_guess();

    int cycle = 0;
    fprintf(outfile, "\n\n\t*=================================================================================*\n"
                     "\t* Cycle  RMS [F, Kappa]   RMS Lambda Error   delta E        Total Energy     DIIS *\n"
                     "\t*---------------------------------------------------------------------------------*\n");
    if(options_.get_str("ALGORITHM") == "TWOSTEP"){
        // This is the two-step update - in each macro iteration, update the orbitals first, then update lambda
        // to self-consistency, until converged.  When lambda is converged and only one scf cycle is needed to reach
        // the desired cutoff, we're done
        SharedMatrix tmp = boost::shared_ptr<Matrix>(new Matrix("temp", nirrep_, nsopi_, nsopi_));
        // Set up the DIIS manager for the density cumulant and SCF iterations
        dpdbuf4 Laa, Lab, Lbb;
        dpd_buf4_init(&Laa, PSIF_LIBTRANS_DPD, 0, ID("[O,O]"), ID("[V,V]"),
                      ID("[O,O]"), ID("[V,V]"), 0, "Lambda <OO|VV>");
        dpd_buf4_init(&Lab, PSIF_LIBTRANS_DPD, 0, ID("[O,o]"), ID("[V,v]"),
                      ID("[O,o]"), ID("[V,v]"), 0, "Lambda <Oo|Vv>");
        dpd_buf4_init(&Lbb, PSIF_LIBTRANS_DPD, 0, ID("[o,o]"), ID("[v,v]"),
                      ID("[o,o]"), ID("[v,v]"), 0, "Lambda <oo|vv>");
        DIISManager scfDiisManager(maxdiis_, "DCFT DIIS Orbitals",DIISManager::LargestError,DIISManager::InCore);
        scfDiisManager.set_error_vector_size(2, DIISEntry::Matrix, scf_error_a_.get(),
                                                DIISEntry::Matrix, scf_error_b_.get());
        scfDiisManager.set_vector_size(2, DIISEntry::Matrix, Fa_.get(),
                                          DIISEntry::Matrix, Fb_.get());
        DIISManager lambdaDiisManager(maxdiis_, "DCFT DIIS Lambdas",DIISManager::LargestError,DIISManager::InCore);
        lambdaDiisManager.set_error_vector_size(3, DIISEntry::DPDBuf4, &Laa,
                                                   DIISEntry::DPDBuf4, &Lab,
                                                   DIISEntry::DPDBuf4, &Lbb);
        lambdaDiisManager.set_vector_size(3, DIISEntry::DPDBuf4, &Laa,
                                             DIISEntry::DPDBuf4, &Lab,
                                             DIISEntry::DPDBuf4, &Lbb);
        dpd_buf4_close(&Laa);
        dpd_buf4_close(&Lab);
        dpd_buf4_close(&Lbb);
        old_ca_->copy(Ca_);
        old_cb_->copy(Cb_);
        // Just so the correct value is printed in the first macro iteration
        scf_convergence_ = compute_scf_error_vector();
        // Start macro-iterations
        while((!scfDone || !lambdaDone) && cycle++ < maxiter_){
            int nLambdaIterations = 0;
            lambdaDiisManager.reset_subspace();
            fprintf(outfile, "\t                          *** Macro Iteration %d ***\n"
                             "\tCumulant Iterations\n",cycle);
            lambdaDone = false;
            // Start density cumulant (lambda) iterations
            while(!lambdaDone && nLambdaIterations++ < options_.get_int("LAMBDA_MAXITER")){
                std::string diisString;
                // Build SO basis tensors for the <VV||VV>, <vv||vv>, and <Vv|Vv> terms in the G intermediate
                build_tensors();
                // Build G and F intermediates needed for the density cumulant residual equations and DCFT energy computation
                build_intermediates();
                // Compute the residuals for density cumulant equations
                lambda_convergence_ = compute_lambda_residual();
                // Update density cumulant tensor
                update_lambda_from_residual();
                if(lambda_convergence_ < diis_start_thresh_){
                    //Store the DIIS vectors
                    dpdbuf4 Laa, Lab, Lbb, Raa, Rab, Rbb, J;
                    dpd_buf4_init(&Raa, PSIF_DCFT_DPD, 0, ID("[O,O]"), ID("[V,V]"),
                                  ID("[O,O]"), ID("[V,V]"), 0, "R <OO|VV>");
                    dpd_buf4_init(&Rab, PSIF_DCFT_DPD, 0, ID("[O,o]"), ID("[V,v]"),
                                  ID("[O,o]"), ID("[V,v]"), 0, "R <Oo|Vv>");
                    dpd_buf4_init(&Rbb, PSIF_DCFT_DPD, 0, ID("[o,o]"), ID("[v,v]"),
                                  ID("[o,o]"), ID("[v,v]"), 0, "R <oo|vv>");
                    dpd_buf4_init(&Laa, PSIF_DCFT_DPD, 0, ID("[O,O]"), ID("[V,V]"),
                                  ID("[O,O]"), ID("[V,V]"), 0, "Lambda <OO|VV>");
                    dpd_buf4_init(&Lab, PSIF_DCFT_DPD, 0, ID("[O,o]"), ID("[V,v]"),
                                  ID("[O,o]"), ID("[V,v]"), 0, "Lambda <Oo|Vv>");
                    dpd_buf4_init(&Lbb, PSIF_DCFT_DPD, 0, ID("[o,o]"), ID("[v,v]"),
                                  ID("[o,o]"), ID("[v,v]"), 0, "Lambda <oo|vv>");

    //                    dpd_buf4_init(&J, PSIF_DCFT_DPD, 0, ID("[O,o]"), ID("[V,v]"),
    //                                  ID("[O,o]"), ID("[V,v]"), 0, "R <Oo|Vv>");
    //                    fprintf(outfile, "DOT = %f\n",dpd_buf4_dot(&Rab, &J));
    //                    dpd_buf4_close(&J);

                    if(lambdaDiisManager.add_entry(6, &Raa, &Rab, &Rbb, &Laa, &Lab, &Lbb)){
                        diisString += "S";
                    }
                    if(lambdaDiisManager.subspace_size() >= mindiisvecs_ && maxdiis_ > 0){
                        diisString += "/E";
                        lambdaDiisManager.extrapolate(3, &Laa, &Lab, &Lbb);
//                        lambdaDiisManager.reset_subspace();
//                    }else{
//                        update_lambda_from_residual();
                    }
                    dpd_buf4_close(&Raa);
                    dpd_buf4_close(&Rab);
                    dpd_buf4_close(&Rbb);
                    dpd_buf4_close(&Laa);
                    dpd_buf4_close(&Lab);
                    dpd_buf4_close(&Lbb);
//                }else{
//                    update_lambda_from_residual();
                }
                // Save old DCFT energy
#if ENERGY_NEW
                old_total_energy_ = new_total_energy_;
#else
                oldEnergy = new_total_energy_;
#endif
                // Compute new DCFT energy
                compute_dcft_energy();
#if ENERGY_NEW
                new_total_energy_ = scf_energy_ + dcft_energy_;
                // Check convergence for density cumulant iterations
                lambdaDone = lambda_convergence_ < lambda_threshold_ &&
                                fabs(new_total_energy_ - old_total_energy_) < lambda_threshold_;
#else
                // Check convergence for density cumulant iterations
                lambdaDone = lambda_convergence_ < lambda_threshold_ &&
                                fabs(new_total_energy_ - oldEnergy) < lambda_threshold_;
#endif
#if ENERGY_NEW
                fprintf(outfile, "\t* %-3d   %12.3e      %12.3e   %12.3e  %21.15f  %-3s *\n",
                        nLambdaIterations, scf_convergence_, lambda_convergence_, new_total_energy_ - old_total_energy_,
                        new_total_energy_, diisString.c_str());
#else
                fprintf(outfile, "\t* %-3d   %12.3e      %12.3e   %12.3e  %21.15f  %-3s *\n",
                        nLambdaIterations, scf_convergence_, lambda_convergence_, new_total_energy_ - oldEnergy,
                        new_total_energy_, diisString.c_str());
#endif
                fflush(outfile);
            }
            // Build new Tau from the density cumulant in the MO basis and transform it the SO basis
            build_tau();
            // Update the orbitals
            int nSCFCycles = 0;
            densityConverged = false;
            scfDiisManager.reset_subspace();
            fprintf(outfile, "\tOrbital Updates\n");
            while((!densityConverged || scf_convergence_ > scf_threshold_)
                    && nSCFCycles++ < options_.get_int("SCF_MAXITER")){
                std::string diisString;
                // Copy core hamiltonian into the Fock matrix array: F = H
                Fa_->copy(so_h_);
                Fb_->copy(so_h_);
                // Build the new Fock matrix from the SO integrals: F += Gbar * kappa
                process_so_ints();
                // Save old SCF energy
#if ENERGY_NEW
                old_total_energy_ = new_total_energy_;
#else
                oldEnergy = new_total_energy_;
#endif
                // Add non-idempotent density contribution (Tau) to the Fock matrix: F += Gbar * tau
                Fa_->add(g_tau_a_);
                Fb_->add(g_tau_b_);
                // Back up the SO basis Fock before it is symmetrically orthogonalized to transform it to the MO basis
                moFa_->copy(Fa_);
                moFb_->copy(Fb_);
                // Compute new SCF energy
                compute_scf_energy();
                // Check SCF convergence
                scf_convergence_ = compute_scf_error_vector();
                if(scf_convergence_ < diis_start_thresh_){
                    if(scfDiisManager.add_entry(4, scf_error_a_.get(), scf_error_b_.get(), Fa_.get(), Fb_.get()))
                        diisString += "S";
                }
                if(scfDiisManager.subspace_size() > mindiisvecs_){
                    diisString += "/E";
                    scfDiisManager.extrapolate(2, Fa_.get(), Fb_.get());
                }
                // Save the Fock matrix before the symmetric orthogonalization for Tau^2 correction computation
                Fa_copy->copy(Fa_);
                Fb_copy->copy(Fb_);
                // Transform the Fock matrix to the symmetrically orhogonalized basis set and digonalize it
                // Obtain new orbitals
                Fa_->transform(s_half_inv_);
                Fa_->diagonalize(tmp, epsilon_a_);
                old_ca_->copy(Ca_);
                Ca_->gemm(false, false, 1.0, s_half_inv_, tmp, 0.0);
                Fb_->transform(s_half_inv_);
                Fb_->diagonalize(tmp, epsilon_b_);
                old_cb_->copy(Cb_);
                Cb_->gemm(false, false, 1.0, s_half_inv_, tmp, 0.0);
                // Make sure that the orbital phase is retained
                correct_mo_phases(false);
                if(!lock_occupation_) find_occupation(epsilon_a_);
                // Update SCF density (kappa)
                densityConverged = update_scf_density() < scf_threshold_;
                // Compute the DCFT energy
#if ENERGY_NEW
                new_total_energy_ = scf_energy_ + dcft_energy_;
                fprintf(outfile, "\t* %-3d   %12.3e      %12.3e   %12.3e  %21.15f  %-3s *\n",
                    nSCFCycles, scf_convergence_, lambda_convergence_, new_total_energy_ - old_total_energy_,
                    new_total_energy_, diisString.c_str());
#else
                compute_dcft_energy();
                fprintf(outfile, "\t* %-3d   %12.3e      %12.3e   %12.3e  %21.15f  %-3s *\n",
                    nSCFCycles, scf_convergence_, lambda_convergence_, new_total_energy_ - oldEnergy,
                    new_total_energy_, diisString.c_str());
#endif
                fflush(outfile);
            }
            // Write orbitals to the checkpoint file
            write_orbitals_to_checkpoint();
            scfDone = nSCFCycles == 1;
            // Transform the Fock matrix to the MO basis
            moFa_->transform(Ca_);
            moFb_->transform(Cb_);
            // Transform one-electron and two-electron integrals to the MO basis using new orbitals
            transform_integrals();
        }
    }else{
        // This is the simultaneous orbital/lambda update algorithm
        SharedMatrix tmp = boost::shared_ptr<Matrix>(new Matrix("temp", nirrep_, nsopi_, nsopi_));
        // Set up the DIIS manager
        DIISManager diisManager(maxdiis_, "DCFT DIIS vectors");
        dpdbuf4 Laa, Lab, Lbb;
        dpd_buf4_init(&Laa, PSIF_LIBTRANS_DPD, 0, ID("[O,O]"), ID("[V,V]"),
                      ID("[O,O]"), ID("[V,V]"), 0, "Lambda <OO|VV>");
        dpd_buf4_init(&Lab, PSIF_LIBTRANS_DPD, 0, ID("[O,o]"), ID("[V,v]"),
                      ID("[O,o]"), ID("[V,v]"), 0, "Lambda <Oo|Vv>");
        dpd_buf4_init(&Lbb, PSIF_LIBTRANS_DPD, 0, ID("[o,o]"), ID("[v,v]"),
                      ID("[o,o]"), ID("[v,v]"), 0, "Lambda <oo|vv>");
        diisManager.set_error_vector_size(5, DIISEntry::Matrix, scf_error_a_.get(),
                                             DIISEntry::Matrix, scf_error_b_.get(),
                                             DIISEntry::DPDBuf4, &Laa,
                                             DIISEntry::DPDBuf4, &Lab,
                                             DIISEntry::DPDBuf4, &Lbb);
        diisManager.set_vector_size(5, DIISEntry::Matrix, Fa_.get(),
                                       DIISEntry::Matrix, Fb_.get(),
                                       DIISEntry::DPDBuf4, &Laa,
                                       DIISEntry::DPDBuf4, &Lab,
                                       DIISEntry::DPDBuf4, &Lbb);
        dpd_buf4_close(&Laa);
        dpd_buf4_close(&Lab);
        dpd_buf4_close(&Lbb);
        while((!scfDone || !lambdaDone || !densityConverged || !energyConverged)
                && cycle++ < maxiter_){
            std::string diisString;
#if ENERGY_NEW
            old_total_energy_ = new_total_energy_;
#else
            oldEnergy = new_total_energy_;
#endif
            build_tau();
            Fa_->copy(so_h_);
            Fb_->copy(so_h_);
            // This will build the new Fock matrix from the SO integrals
            process_so_ints();
            Fa_->add(g_tau_a_);
            Fb_->add(g_tau_b_);
            // Copy and transform the Fock matrix, before it's symmetrically orthogonalized
            moFa_->copy(Fa_);
            moFb_->copy(Fb_);
            moFa_->transform(Ca_);
            moFb_->transform(Cb_);
            compute_scf_energy();
#if ENERGY_NEW
            new_total_energy_ = scf_energy_;
#endif
            scf_convergence_ = compute_scf_error_vector();
            scfDone = scf_convergence_ < scf_threshold_;
            build_intermediates();
            lambda_convergence_ = compute_lambda_residual();
            lambdaDone = lambda_convergence_ < lambda_threshold_;
            update_lambda_from_residual();
            compute_dcft_energy();
#if ENERGY_NEW
            new_total_energy_ += dcft_energy_;
            energyConverged = fabs(old_total_energy_ - new_total_energy_) < lambda_convergence_;
#else
            energyConverged = fabs(oldEnergy - new_total_energy_) < lambda_convergence_;
#endif
            if(scf_convergence_ < diis_start_thresh_ && lambda_convergence_ < diis_start_thresh_){
                //Store the DIIS vectors
                dpdbuf4 Laa, Lab, Lbb, Raa, Rab, Rbb;
                dpd_buf4_init(&Raa, PSIF_DCFT_DPD, 0, ID("[O,O]"), ID("[V,V]"),
                              ID("[O,O]"), ID("[V,V]"), 0, "R <OO|VV>");
                dpd_buf4_init(&Rab, PSIF_DCFT_DPD, 0, ID("[O,o]"), ID("[V,v]"),
                              ID("[O,o]"), ID("[V,v]"), 0, "R <Oo|Vv>");
                dpd_buf4_init(&Rbb, PSIF_DCFT_DPD, 0, ID("[o,o]"), ID("[v,v]"),
                              ID("[o,o]"), ID("[v,v]"), 0, "R <oo|vv>");
                dpd_buf4_init(&Laa, PSIF_DCFT_DPD, 0, ID("[O,O]"), ID("[V,V]"),
                              ID("[O,O]"), ID("[V,V]"), 0, "Lambda <OO|VV>");
                dpd_buf4_init(&Lab, PSIF_DCFT_DPD, 0, ID("[O,o]"), ID("[V,v]"),
                              ID("[O,o]"), ID("[V,v]"), 0, "Lambda <Oo|Vv>");
                dpd_buf4_init(&Lbb, PSIF_DCFT_DPD, 0, ID("[o,o]"), ID("[v,v]"),
                              ID("[o,o]"), ID("[v,v]"), 0, "Lambda <oo|vv>");
                if(diisManager.add_entry(10, scf_error_a_.get(), scf_error_b_.get(), &Raa, &Rab, &Rbb,
                                           Fa_.get(), Fb_.get(), &Laa, &Lab, &Lbb)){
                    diisString += "S";
                }
                if(diisManager.subspace_size() > mindiisvecs_){
                    diisString += "/E";
                    diisManager.extrapolate(5, Fa_.get(), Fb_.get(), &Laa, &Lab, &Lbb);
                }
                dpd_buf4_close(&Raa);
                dpd_buf4_close(&Rab);
                dpd_buf4_close(&Rbb);
                dpd_buf4_close(&Laa);
                dpd_buf4_close(&Lab);
                dpd_buf4_close(&Lbb);
            }

            // Save the Fock matrix before the orthogonalization
            Fa_copy->copy(Fa_);
            Fb_copy->copy(Fb_);

            Fa_->transform(s_half_inv_);
            Fa_->diagonalize(tmp, epsilon_a_);
            old_ca_->copy(Ca_);
            Ca_->gemm(false, false, 1.0, s_half_inv_, tmp, 0.0);
            Fb_->transform(s_half_inv_);
            Fb_->diagonalize(tmp, epsilon_b_);
            old_cb_->copy(Cb_);
            Cb_->gemm(false, false, 1.0, s_half_inv_, tmp, 0.0);
            if(!correct_mo_phases(false)){
                fprintf(outfile,"\t\tThere was a problem correcting the MO phases.\n"
                                "\t\tIf this does not converge, try ALGORITHM=TWOSTEP\n");
            }
            write_orbitals_to_checkpoint();
            transform_integrals();
            if(!lock_occupation_) find_occupation(epsilon_a_);
            densityConverged = update_scf_density() < scf_threshold_;
            // If we've performed enough lambda updates since the last orbitals
            // update, reset the counter so another SCF update is performed
#if ENERGY_NEW
            fprintf(outfile, "\t* %-3d   %12.3e      %12.3e   %12.3e  %21.15f  %-3s *\n",
                    cycle, scf_convergence_, lambda_convergence_, new_total_energy_ - old_total_energy_,
                    new_total_energy_, diisString.c_str());
#else
            fprintf(outfile, "\t* %-3d   %12.3e      %12.3e   %12.3e  %21.15f  %-3s *\n",
                    cycle, scf_convergence_, lambda_convergence_, new_total_energy_ - oldEnergy,
                    new_total_energy_, diisString.c_str());
#endif
            fflush(outfile);
        }
    }
    if(!scfDone || !lambdaDone || !densityConverged)
        throw ConvergenceError<int>("DCFT", maxiter_, lambda_threshold_,
                               lambda_convergence_, __FILE__, __LINE__);

    fprintf(outfile, "\t*=================================================================================*\n");

    // Computes the Tau^2 correction to Tau and the DCFT energy if requested by the user
    if(options_.get_bool("TAU_SQUARED")){
        Fa_->copy(Fa_copy);
        Fb_->copy(Fb_copy);
        build_tau();
        compute_tau_squared();
        compute_energy_tau_squared();
        new_total_energy_ += energy_tau_squared_;
        fprintf(outfile, "\n\t*DCFT Energy Tau^2 correction          = %20.15f\n", energy_tau_squared_);
    }

    // Tau^2 should probably be added to SCF energy....
    fprintf(outfile, "\n\t*DCFT SCF Energy                       = %20.15f\n", scf_energy_);
#if ENERGY_NEW
    fprintf(outfile, "\t*DCFT Lambda Energy                    = %20.15f\n", dcft_energy_);
#endif
    fprintf(outfile, "\t*DCFT Total Energy                     = %20.15f\n", new_total_energy_);

    Process::environment.globals["CURRENT ENERGY"] = new_total_energy_;
    Process::environment.globals["DCFT ENERGY"] = new_total_energy_;
    Process::environment.globals["DCFT SCF ENERGY"] = scf_energy_;
#if ENERGY_NEW
    Process::environment.globals["DCFT LAMBDA ENERGY"] = dcft_energy_;
#endif
    Process::environment.globals["DCFT TAU SQUARED CORRECTION"] = energy_tau_squared_;

    if(!options_.get_bool("RELAX_ORBITALS")){
        fprintf(outfile, "Warning!  The orbitals were not relaxed\n");
    }

    print_opdm();

    if(options_.get_bool("COMPUTE_TPDM")) dump_density();
    mulliken_charges();
    check_n_representability();

    if(!options_.get_bool("RELAX_ORBITALS") && options_.get_bool("IGNORE_TAU")){
        psio_->open(PSIF_LIBTRANS_DPD, PSIO_OPEN_OLD);
        /*
         * Comout the CEPA-0 correlation energy
         * E = 1/4 L_IJAB <IJ||AB>
         *        +L_IjAb <Ij|Ab>
         *    +1/4 L_ijab <ij||ab>
         */
        dpdbuf4 I, L;
        // Alpha - Alpha
        dpd_buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[O,O]"), ID("[V,V]"),
                      ID("[O,O]"), ID("[V,V]"), 1, "MO Ints <OO|VV>");
        dpd_buf4_init(&L, PSIF_DCFT_DPD, 0, ID("[O,O]"), ID("[V,V]"),
                      ID("[O,O]"), ID("[V,V]"), 0, "Lambda <OO|VV>");
        double eAA = 0.25 * dpd_buf4_dot(&L, &I);
        dpd_buf4_close(&I);
        dpd_buf4_close(&L);

        // Alpha - Beta
        dpd_buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[O,o]"), ID("[V,v]"),
                      ID("[O,o]"), ID("[V,v]"), 0, "MO Ints <Oo|Vv>");
        dpd_buf4_init(&L, PSIF_DCFT_DPD, 0, ID("[O,o]"), ID("[V,v]"),
                      ID("[O,o]"), ID("[V,v]"), 0, "Lambda <Oo|Vv>");
        double eAB = dpd_buf4_dot(&L, &I);
        dpd_buf4_close(&I);
        dpd_buf4_close(&L);

        // Beta - Beta
        dpd_buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[o,o]"), ID("[v,v]"),
                      ID("[o,o]"), ID("[v,v]"), 1, "MO Ints <oo|vv>");
        dpd_buf4_init(&L, PSIF_DCFT_DPD, 0, ID("[o,o]"), ID("[v,v]"),
                      ID("[o,o]"), ID("[v,v]"), 0, "Lambda <oo|vv>");
        double eBB = 0.25 * dpd_buf4_dot(&L, &I);
        dpd_buf4_close(&I);
        dpd_buf4_close(&L);
        fprintf(outfile, "\t!CEPA0 Total Energy         = %20.15f\n",
                scf_energy_ + eAA + eAB + eBB);
        psio_->close(PSIF_LIBTRANS_DPD, 1);
    }

    // Free up memory and close files
    finalize();
    return(new_total_energy_);
}

}} // Namespaces

