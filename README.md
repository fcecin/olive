# olive
A simple extension of eosio.token with basic income and an identity system based on converting tokens into reputation score.

This is an extension of the wubi token contract (the one used for the Telos network's ACORN token). It pays a basic income, just like wubi, but it introduces the concept of a claim to personhood.

Any account can have an olive token balance, but an account has to be endorsed by another account that is already "personified" (validated) to be able to receive UBI. The first validated account has to be endorsed from the contract account itself.

After an account is endorsed with a positive score, which consumes tokens proportional to the positive score that is being granted (tokens and score are 1:1), an account has to set a "proof of personhood" (pop) attribute, which is a simple string. That string should point to a web site or some other network resource which offers a subjective case for that account being owned and controlled by an unique living individual person.

Any account with a score greater than zero can claim UBI. A negative score suspends the ability to claim the UBI (which continues to accumulate up to 360 days).

Any account with a score equal or greater than 10 can endorse or drain other accounts. When endorsing, a quantity X of tokens is converted in an increase of X in the target account's score. When draining, a quantity X of tokens is converted in a decrease of X in the target account's score.

Setting proof of personhood (--pop), endorsing (--endorse) and draning (--drain) are virtual actions. They are modes of invocation of the default eosio.token transfer() action, and you select them by invoking the appropriate command using the transfer memo field.

So if you want to set your own pop, yoo do the following transfer:

```
from: youraccount
to: youraccount
quantity: 0
memo: --pop Hi my name is XYZ and my website is http://abc.def
```

If you want to endorse someone else by burning 25 of your tokens and crediting +25 score to them, you'd do

```
from: youraccount
to: someoneelse
quantity: 25
memo: --endorse this is an entirely optional message so you record your endorsement
```

And if you want to drain someone's score for whatever reason by spending 10 tokens to apply a -10 score penalty to them:

````
from: youraccount
to: badaccount
quantity: 10
memo: --drain your proof of personhood points to an unexisting URL and that's bad for the network, no UBI for you
````

These primitives allow for a network of vigilant users, aided by higher-level tools and specialized human roles, to build a version of the wubi basic income system that is Sybil-resistant.  
