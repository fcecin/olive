/**
 *  Source code for the Olive contract.
 *
 *  This is a modification of the standard eosio.token contract.
 *  All modifications are in the public domain.
 */

#include <olive.hpp>

namespace eosio {

void token::create( name   issuer,
                    asset  maximum_supply )
{
    require_auth( _self );

    auto sym = maximum_supply.symbol;
    check( sym.is_valid(), "invalid symbol name" );
    check( maximum_supply.is_valid(), "invalid supply");
    check( maximum_supply.amount > 0, "max-supply must be positive");

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    check( existing == statstable.end(), "token with symbol already exists" );

    statstable.emplace( _self, [&]( auto& s ) {
       s.supply.symbol = maximum_supply.symbol;
       s.max_supply    = maximum_supply;
       s.issuer        = issuer;
    });
}

 
void token::issue( name to, asset quantity, string memo )
{
    // issue() is only needed to bootstrap UBI payment checking, which requires tokens. 

    auto sym = quantity.symbol;
    check( sym.is_valid(), "invalid symbol name" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    check( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
    const auto& st = *existing;

    require_auth( st.issuer );
    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must issue positive quantity" );

    check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    check( quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

    statstable.modify( st, same_payer, [&]( auto& s ) {
       s.supply += quantity;
    });

    add_balance( st.issuer, quantity, st.issuer );

    if( to != st.issuer ) {
      SEND_INLINE_ACTION( *this, transfer, { {st.issuer, "active"_n} },
                          { st.issuer, to, quantity, memo }
      );
    }
}

void token::retire( asset quantity, string memo )
{
    auto sym = quantity.symbol;
    check( sym.is_valid(), "invalid symbol name" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    check( existing != statstable.end(), "token with symbol does not exist" );
    const auto& st = *existing;

    // If the issuer is set to this contract, then anyone can retire the tokens.
    if (st.issuer != _self)
      require_auth( st.issuer );

    require_auth( st.issuer );
    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must retire positive quantity" );

    check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

    statstable.modify( st, same_payer, [&]( auto& s ) {
       s.supply -= quantity;
    });

    sub_balance( st.issuer, quantity );
}

// The transfer action is heavily customized. 
void token::transfer( name    from,
                      name    to,
                      asset   quantity,
                      string  memo )
{
    // We will use transfers to self as a way to log UBI issuance, so we have to allow them.
    //check( from != to, "cannot transfer to self" );
    
    require_auth( from );
    check( is_account( to ), "to account does not exist");
    auto sym = quantity.symbol.code();
    stats statstable( _self, sym.raw() );
    const auto& st = statstable.get( sym.raw() );

    require_recipient( from );
    require_recipient( to );

    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    auto payer = has_auth( to ) ? to : from;

    // --- Check for special memo commands (endorse or drain score, or set proof of personhood for self) 

    if ( memo == "--pop" ) {
      try_pop( from, to, "", quantity, payer ); // from == to or from == _self
      return;
    }

    if ( memo.substr(0, 6) == "--pop ") {
      try_pop( from, to, memo.substr(6, string::npos), quantity, payer ); // from == to or from == _self
      return;
    }

    if ( (memo == "--endorse") || (memo.substr(0, 10) == "--endorse ") ) {
      try_endorse( from, to, quantity, payer, statstable, st ); // quantity must be greater than zero, from == _self equals from == to
      return;
    }

    if ( ( memo == "--drain") || (memo.substr(0, 8) == "--drain ") ) {
      try_drain( from, to, quantity, payer, statstable, st ); // quantity must be greater than zero, from == _self equals from == to
      return;
    }

    // --- It's not one of the special memo commands, so at this point it's a regular transfer.

    check( quantity.amount > 0, "must transfer positive quantity" );
    
    // Sending to self is a no-op. Just log it.
    if (from == to)
      return;

    // Check for an UBI claim.
    try_ubi_claim( from, quantity.symbol, payer, statstable, st );
    
    sub_balance( from, quantity );
    add_balance( to, quantity, payer );
}

void token::sub_balance( name owner, asset value ) {
   accounts from_acnts( _self, owner.value );

   const auto& from = from_acnts.get( value.symbol.code().raw(), "no balance object found" );
   check( from.balance.amount >= value.amount, "overdrawn balance" );

   from_acnts.modify( from, owner, [&]( auto& a ) {
         a.balance -= value;
      });
}

void token::add_balance( name owner, asset value, name ram_payer )
{
   accounts to_acnts( _self, owner.value );
   auto to = to_acnts.find( value.symbol.code().raw() );
   if( to == to_acnts.end() ) {
      to_acnts.emplace( ram_payer, [&]( auto& a ){
        a.balance = value;
      });
   } else {
      to_acnts.modify( to, same_payer, [&]( auto& a ) {
        a.balance += value;
      });
   }
}

void token::open( name owner, const symbol& symbol, name ram_payer )
{
   require_auth( ram_payer );

   auto sym_code_raw = symbol.code().raw();

   stats statstable( _self, sym_code_raw );
   const auto& st = statstable.get( sym_code_raw, "symbol does not exist" );
   check( st.supply.symbol == symbol, "symbol precision mismatch" );

   accounts acnts( _self, owner.value );
   auto it = acnts.find( sym_code_raw );
   if( it == acnts.end() ) {
      acnts.emplace( ram_payer, [&]( auto& a ){
        a.balance = asset{0, symbol};
      });
   }
}

void token::close( name owner, const symbol& symbol )
{
   require_auth( owner );
   accounts acnts( _self, owner.value );
   auto it = acnts.find( symbol.code().raw() );
   check( it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect." );
   check( it->balance.amount == 0, "Cannot close because the balance is not zero." );
   acnts.erase( it );

   // delete persons table row unconditionally too, if there is any
   persons prns( _self, owner.value );
   auto itx = prns.find( symbol.code().raw() );
   prns.erase( itx );
}

void token::endorse( name from, name to, asset quantity, string memo )
{
  require_auth( from );
  check( is_account( to ), "to account does not exist");
  auto sym = quantity.symbol.code();
  stats statstable( _self, sym.raw() );
  const auto& st = statstable.get( sym.raw() );

  require_recipient( from );
  require_recipient( to );

  check( quantity.is_valid(), "invalid quantity" );
  check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
  check( memo.size() <= 256, "memo has more than 256 bytes" );

  auto payer = has_auth( to ) ? to : from;

  try_endorse( from, to, quantity, payer, statstable, st );
}

void token::drain( name from, name to, asset quantity, string memo )
{
  require_auth( from );
  check( is_account( to ), "to account does not exist");
  auto sym = quantity.symbol.code();
  stats statstable( _self, sym.raw() );
  const auto& st = statstable.get( sym.raw() );

  require_recipient( from );
  require_recipient( to );

  check( quantity.is_valid(), "invalid quantity" );
  check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
  check( memo.size() <= 256, "memo has more than 256 bytes" );

  auto payer = has_auth( to ) ? to : from;

  try_drain( from, to, quantity, payer, statstable, st );
}

// NOTE: This action is hard-coded for the "OLIVE" token symbol.
// Having users enter a "4,OLIVE" symbol parameter is error-prone and pointless.
void token::setpop( name owner, string pop )
{
  check( pop != "[DEFAULT]", "reserved proof-of-personhood value" );

  require_auth( owner );
  require_recipient( owner );
  
  // Locate the person record (if not found, this account doesn't claim to belong to an unique person)
  const uint64_t symbol_code_raw = 297800387663;  // The OLIVE token
  persons from_prns( _self, owner.value );
  auto itx = from_prns.find( symbol_code_raw );
  check( itx != from_prns.end(), "this account has not been endorsed yet" );

  // Update person record (note: "owner" is the RAM payer for the new pop string)
  from_prns.modify( *itx, owner, [&]( auto& a ) {
      a.pop = pop;
    });
}

void token::try_pop( name from, name to, string new_pop, asset quantity, name payer )
{
  check( ((to == from) || (to == _self)), "from and to must be set to self or the contract account when updating proof-of-personhood" );

  check( new_pop != "[DEFAULT]", "reserved proof-of-personhood value" );

  // Locate the person record (if not found, this account doesn't claim to belong to an unique person)
  persons from_prns( _self, from.value );
  auto itx = from_prns.find( quantity.symbol.code().raw() );
  check( itx != from_prns.end(), "this account has not been endorsed yet" );

  // Update person record (note: "payer" is probably "from", i.e. the user will pay the RAM for their own pop string)
  from_prns.modify( *itx, payer, [&]( auto& a ) {
      a.pop = new_pop;
    });
}

void token::try_endorse( name from, name to, asset quantity, name payer, stats& statstable, const currency_stats& st )
{
  check( quantity.amount > 0, "must burn a positive quantity to endorse an account" );

  // the smart contract account is used as a substitute for transfers to self (for wallets that don't allow
  //   transfers to self)
  if (to == _self)
    to = from;
  
  // The contract account can execute unlimited endorsements without spending tokens
  const bool sudo = (from == _self);

  if (! sudo) {

    // Locate the endorser (from) person record (if not found, that account doesn't claim to belong to an unique person)
    persons from_prns( _self, from.value );
    auto itx = from_prns.find( quantity.symbol.code().raw() );
    check( itx != from_prns.end(), "from account has not been endorsed yet" );

    const auto & from_person = *itx;

    // Endorser minimum score
    check( from_person.score >= endorse_minimum_score * get_precision_multiplier(quantity.symbol), "from account score too low" );

    // Endorser must claim personhood
    check( !is_empty_pop(from_person.pop), "from account has no proof-of-personhood set" );
  }
  
  // Locate the endorsed (to) person record
  persons to_prns( _self, to.value );
  auto to_itx = to_prns.find( quantity.symbol.code().raw() );
  if (to_itx == to_prns.end()) {
    // Not found, so create it by paying for its RAM and init the score to the endorsement amount
    to_prns.emplace( payer, [&]( auto& a ){
      a.symbol_code_raw = quantity.symbol.code().raw();
      a.score = quantity.amount;
      a.last_claim_day = get_today() + 1; // 2-day waiting period before "to" can claim UBI
      a.pop = "[DEFAULT]";
    });

    // Also, to endorse is to open() the token balance for the person being endorsed, if necessary.
    // This check is only needed if the person record was not found. If it was found, then the
    //   token balance already exists (you can't ever have a person record mapping to an account
    //   without a balance record).
    accounts acnts( _self, to.value );
    auto it = acnts.find( quantity.symbol.code().raw() );
    if( it == acnts.end() ) {
      acnts.emplace( payer, [&]( auto& a ){
          a.balance = asset{0, quantity.symbol};
        });
    }
  } else {
    // Found, so increase the score by the endorsement amount
    to_prns.modify( *to_itx, payer, [&]( auto& a ) {
        a.score = (int32_t) std::min( (int64_t) ( std::numeric_limits<int32_t>::max() ), (int64_t) ( a.score + quantity.amount ) );
      });
  }

  if (! sudo) {
  
    // Retire tokens spent
    statstable.modify( st, same_payer, [&]( auto& s ) {
        s.supply -= quantity;
      });
    sub_balance( from, quantity );
  }
}

void token::try_drain( name from, name to, asset quantity, name payer, stats& statstable, const currency_stats& st )
{
  check( quantity.amount > 0, "must burn a positive quantity to drain an account" );

  // the smart contract account is used as a substitute for transfers to self (for wallets that don't allow
  //   transfers to self)
  if (to == _self)
    to = from;
  
  // The contract account can execute unlimited score draining without spending tokens
  const bool sudo = (from == _self);

  if (! sudo) {
  
    // Locate the drainer (from) person record (if not found, that account doesn't claim to belong to an unique person)
    persons from_prns( _self, from.value );
    auto itx = from_prns.find( quantity.symbol.code().raw() );
    check( itx != from_prns.end(), "from account has not been endorsed yet" );
    
    const auto & from_person = *itx;
    
    // Drainer minimum score
    check( from_person.score >= endorse_minimum_score * get_precision_multiplier(quantity.symbol), "from account score too low" );
    
    // Drainer must claim personhood
    check( !is_empty_pop(from_person.pop), "from account has no proof-of-personhood set" );
  }
  
  // Locate the endorsed (to) person record (if not found, we cannot drain it)
  persons to_prns( _self, to.value );
  auto to_itx = to_prns.find( quantity.symbol.code().raw() );
  check( to_itx != to_prns.end(), "to account has not been endorsed yet" );

  // Subtract score from to
  to_prns.modify( *to_itx, payer, [&]( auto& a ) {
      a.score = (int32_t) std::max( (int64_t) ( std::numeric_limits<int32_t>::min() ), (int64_t) ( a.score - quantity.amount ) );
    });

  if (! sudo) {
    
    // Retire tokens spent
    statstable.modify( st, same_payer, [&]( auto& s ) {
        s.supply -= quantity;
      });
    sub_balance( from, quantity );
  }
}

void token::try_ubi_claim( name from, const symbol& sym, name payer, stats& statstable, const currency_stats& st )
{
  // Locate the person record (if not found, this account doesn't claim to belong to an unique person)
  persons from_prns( _self, from.value );
  auto itx = from_prns.find( sym.code().raw() );
  if (itx == from_prns.end())
    return;

  const auto & from_person = *itx;

  // If the person has a zero or negative score, they can't claim UBI right now. (Silent failure)
  if (from_person.score <= 0)
    return;

  // If the person has an empty or default pop they can't claim UBI right now. (Silent failure)
  if (is_empty_pop(from_person.pop))
    return;
  
  const time_type today = get_today();
  
  if (from_person.last_claim_day < today) {
    
    // The UBI grants 1 token per day per account. 
    
    // Compute the claim amount relative to days elapsed since the last claim, excluding today's pay.
    // If you claimed yesterday, this is zero.
    int64_t claim_amount = today - from_person.last_claim_day - 1;
    // The limit for claiming accumulated past income is 360 days/coins. Unclaimed tokens past that
    //   one year maximum of accumulation are lost.
    time_type lost_days = 0;
    if (claim_amount > max_past_claim_days) {
      lost_days = claim_amount - max_past_claim_days;
      claim_amount = max_past_claim_days;
    }
    // You always claim for the current day only (plus accumulated days, that was computed above).
    ++ claim_amount;
    
    int64_t precision_multiplier = get_precision_multiplier(sym);
    asset claim_quantity = asset{claim_amount * precision_multiplier, sym};
    
    // Respect the max_supply limit for UBI issuance.
    int64_t available_amount = st.max_supply.amount - st.supply.amount;
    if (claim_quantity.amount > available_amount)
      claim_quantity.set_amount(available_amount);
    
    time_type last_claim_day_delta = lost_days + (claim_quantity.amount / precision_multiplier);
    
    if (claim_quantity.amount > 0) {
      
      // Log this basic income payment with a fake inline transfer action to self.
      log_claim( from, claim_quantity, from_person.score, from_person.last_claim_day + last_claim_day_delta, lost_days );
      
      // Update the token total supply.
      statstable.modify( st, same_payer, [&]( auto& s ) {
	  s.supply += claim_quantity;
        });
      
      // Finally, move the claim date window proportional to the amount of days of income we claimed
      //   (and also account for days of income that have been forever lost)
      from_prns.modify( from_person, from, [&]( auto& a ) {
	  a.last_claim_day += last_claim_day_delta;
	});
      
      // Pay the user doing the transfer ("from").
      add_balance( from, claim_quantity, payer );
    }
  }
}

// This calls a transfer-to-self just to log a memo that explains what the UBI payment was.
void token::log_claim( name claimant, asset claim_quantity, int32_t cur_score, time_type next_last_claim_day, time_type lost_days )
{
  string claim_memo = "[UBI] +";
  claim_memo.append( claim_quantity.to_string() );
  claim_memo.append(" (score: " );
  claim_memo.append( std::to_string( ((double)cur_score) / ((double)get_precision_multiplier(claim_quantity.symbol))) );
  claim_memo.append(", next: " );
  claim_memo.append( days_to_string(next_last_claim_day + 1) );
  claim_memo.append( ")" );
  if (lost_days > 0) {
    claim_memo.append(" (lost: ");
    claim_memo.append( std::to_string(lost_days) );
    claim_memo.append(" days of income)");
  }

  SEND_INLINE_ACTION( *this, transfer, { {claimant, "active"_n} },
		      { claimant, claimant, claim_quantity, claim_memo }
  );
}

// Input is days since epoch
string token::days_to_string( int64_t days )
{
  // https://stackoverflow.com/questions/7960318/math-to-convert-seconds-since-1970-into-date-and-vice-versa
  // http://howardhinnant.github.io/date_algorithms.html
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(days - era * 146097);       // [0, 146096]
  const unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;  // [0, 399]
  const int64_t y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);                // [0, 365]
  const unsigned mp = (5*doy + 2)/153;                                   // [0, 11]
  const unsigned d = doy - (153*mp+2)/5 + 1;                             // [1, 31]
  const unsigned m = mp + (mp < 10 ? 3 : -9);                            // [1, 12]
  
  string s = std::to_string(d);
  if (s.length() == 1)
    s = "0" + s;
  s.append("-");
  string ms = std::to_string(m);
  if (ms.length() == 1)
    ms = "0" + ms;
  s.append( ms );
  s.append("-");
  s.append( std::to_string(y + (m <= 2)) );
  return s;
}

} /// namespace eosio

EOSIO_DISPATCH( eosio::token, (create)(issue)(transfer)(open)(close)(retire)(endorse)(drain)(setpop) )
