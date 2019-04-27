/**
 *  This is Olive, a basic income token contract with identity and reputation 
 *    primitives that can be used to implement Sybil-resistant systems.
 *
 *  This is a modification of the standard eosio.token contract.
 *  All modifications are in the public domain.
 */
#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/transaction.hpp>

#include <string>

namespace eosiosystem {
   class system_contract;
}

namespace eosio {

   using std::string;

   class [[eosio::contract("olive")]] token : public contract {
      public:
         using contract::contract;

         [[eosio::action]]
         void create( name   issuer,
                      asset  maximum_supply);

         [[eosio::action]]
         void issue( name to, asset quantity, string memo );

         [[eosio::action]]
         void retire( asset quantity, string memo );

         [[eosio::action]]
         void transfer( name    from,
                        name    to,
                        asset   quantity,
                        string  memo );

         [[eosio::action]]
         void open( name owner, const symbol& symbol, name ram_payer );

         [[eosio::action]]
         void close( name owner, const symbol& symbol );

         static asset get_supply( name token_contract_account, symbol_code sym_code )
         {
            stats statstable( token_contract_account, sym_code.raw() );
            const auto& st = statstable.get( sym_code.raw() );
            return st.supply;
         }

         static asset get_balance( name token_contract_account, name owner, symbol_code sym_code )
         {
            accounts accountstable( token_contract_account, owner.value );
            const auto& ac = accountstable.get( sym_code.raw() );
            return ac.balance;
         }

         using create_action = eosio::action_wrapper<"create"_n, &token::create>;
         using issue_action = eosio::action_wrapper<"issue"_n, &token::issue>;
         using retire_action = eosio::action_wrapper<"retire"_n, &token::retire>;
         using transfer_action = eosio::action_wrapper<"transfer"_n, &token::transfer>;
         using open_action = eosio::action_wrapper<"open"_n, &token::open>;
         using close_action = eosio::action_wrapper<"close"_n, &token::close>;

         // ---- Olive extensions
	 
         [[eosio::action]]
         void endorse( name    from,
                       name    to,
                       asset   quantity,
                       string  memo );

         [[eosio::action]]
         void drain( name    from,
                     name    to,
                     asset   quantity,
                     string  memo );

	 // NOTE: This action is hard-coded for the "OLIVE" token symbol.
         [[eosio::action]]
         void setpop( name    owner,
                      string  pop );

         using endorse_action = eosio::action_wrapper<"endorse"_n, &token::endorse>;
         using drain_action = eosio::action_wrapper<"drain"_n, &token::drain>;
         using setpop_action = eosio::action_wrapper<"setpop"_n, &token::setpop>;

      private:
         struct [[eosio::table]] account {
            asset    balance;

            uint64_t primary_key()const { return balance.symbol.code().raw(); }
         };

         struct [[eosio::table]] currency_stats {
            asset    supply;
            asset    max_supply;
            name     issuer;

            uint64_t primary_key()const { return supply.symbol.code().raw(); }
         };

         typedef eosio::multi_index< "accounts"_n, account > accounts;
         typedef eosio::multi_index< "stat"_n, currency_stats > stats;

         void sub_balance( name owner, asset value );
         void add_balance( name owner, asset value, name ram_payer );

         // ---- Olive extensions

         typedef uint16_t time_type;
         
         struct [[eosio::table]] person {
            uint64_t       symbol_code_raw;
            int32_t        score;           // Reputation of this personhood
            time_type      last_claim_day;  // UBI claim tracker for this personhood
            std::string    pop;             // Proof-of-personhood (e.g. a web URL)

            uint64_t primary_key()const { return symbol_code_raw; }
         };

	 typedef eosio::multi_index< "persons"_n, person > persons;

	 void try_pop( name from, name to, string new_pop, asset quantity );

	 void try_endorse( name from, name to, asset quantity, name payer, stats& statstable, const currency_stats& st );

	 void try_drain( name from, name to, asset quantity, stats& statstable, const currency_stats& st );

	 void try_ubi_claim( name from, const symbol& sym, stats& statstable, const currency_stats& st, bool silent );

	 void log_claim( name claimant, asset claim_quantity, int32_t cur_score, time_type next_last_claim_day, time_type lost_days );
	 
	 static bool is_empty_pop( const string& pop ) { return ( (pop == "") || (pop == "[DEFAULT]")); }

	 static int64_t get_precision_multiplier ( const symbol& symbol ) {
	   int64_t precision_multiplier = 1;
	   for (int i=0; i<symbol.precision(); ++i)
	     precision_multiplier *= 10;
	   return precision_multiplier;
	 }

	 static string days_to_string( int64_t days );

	 static time_type get_today() { return (time_type)(current_time_point().sec_since_epoch() / 86400); }

	 static const int64_t max_past_claim_days = 360;

	 static const int64_t endorse_minimum_score = 10; // in whole units of the currency (so 10 0000)

	 static const int64_t first_endorsement_fee = 1; // in whole units of the currency (so 1 0000)
   };

} /// namespace eosio
