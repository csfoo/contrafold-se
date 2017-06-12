## CONTRAfold-SE - Learning RNA secondary structure (only) from structure probing data

### Installation

Clone the repository and run `make` in `src` to compile CONTRAfold-SE.

CONTRAfold-SE has been tested on g++ 4.4.7 and 4.8.4.

### Usage

Below are some examples for CONTRAfold-SE usage.

Please see the documentation of [CONTRAfold](http://contra.stanford.edu/contrafold/manual_v2_02.pdf) for further information on parameters and usage.

#### Training
Learn the parameters based on a set of sequences, in which sequences with associated probing data have data from 2 sources, and with lambda parameter 0.1.

Assumes that folder "trainset" has a set of sequences of type ".bpseq" in evidence format for the ones with data.

`contrafold train --regularize 1 --numdatasources 2 --maxiter 1000 --hyperparam_data 0.1 --initweights contrafold.params.complementary_data2 trainset/*.bpseq`

If there are a large number of input files used (> 1000 files; e.g. for training on RMDB data), provide a text file containing the list of example files instead with the `--examplefile` option.

`contrafold train --regularize 1 --numdatasources 1 --maxiter 500 --examplefile examples.txt`

#### Prediction with probing data
Predict the structure of sequence "seq" with evidence "seq.bpseq" for a variety of gamma.

Assumes that folder "testset" has a bpseq file "seq.bpseq" in evidence format.

`contrafold predict  --numdatasources 1 --params learned_params.params --gamma -1 --evidence testset/seq.bpseq`

#### Prediction without probing data
Predict the structure of sequence "seq" for a variety of gamma.

Assumes that folder "testset" has a seq file "seq.seq".

`contrafold predict  --numdatasources 1 --params learned_params.params --gamma -1 testset/seq.bpseq`

#### Input file formats
To support structure probing data we adapt the BPSEQ format in two ways to support sequences with only probing data (BPP2SEQ), and sequences with both probing data and known structure (base-pairings) (BPP2TSEQ).

The original BPSEQ format consists of a row for each base in the RNA molecule, describing the index (1-based), the actual base present, and the index of the pairing partner (0 if unpaired). 

In the BPP2SEQ format, there is an evidence string `e<N>` following the base, where `<N>` is an integer denoting how many probing data sources there are, followed by `<N>` (positive) values of the unpairedness potential (derived from probing data) for that base. For instance, `e2` denotes 2 probing sources, and should be followed by a two positive real numbers; all entries should have the same evidence string. An example is shown below

```
1 G e2 7.070000e+00 -1.000000e+00
2 A e2 7.570000e+00 3.333333e-02
3 A e2 6.500000e+00 -1.000000e+00
4 A e2 5.310000e+00 4.444444e-02
```

In the BPP2TSEQ format, the evidence string is `t<N>` instead, and following the unpairedness potential values is the index of the pairing partner.

**In either format, values that are less than 1e-5 are ignored (treated as not present) for numerical stability.**

### Datasets

Data used to train and evaluate CONTRAfold-SE may be obtained [here](http://ai.stanford.edu/~csfoo/contrafold-se/contrafold-se-datasets.tar.gz).
