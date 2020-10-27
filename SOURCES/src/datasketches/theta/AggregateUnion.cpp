#include "Vertica.h"
#include <iostream>
#include <theta_sketch.hpp>
#include <theta_union.hpp>
#include "../../../include/datasketches/theta/theta_common.hpp"

using namespace Vertica;
using namespace std;


class ThetaSketchAggregateUnion : public AggregateFunction {
private:
    uint8_t lg_k;
    uint64_t seed;

public:
    void setup(ServerInterface &srvInterface, const SizedColumnTypes &argTypes) {
        lg_k = readLogK(srvInterface);
        seed = readSeed(srvInterface);
    }

   // use a theta_union object as an aggregation state
   // store a pointer in the IntermediateAggs
    void initAggregate(ServerInterface &srvInterface, IntermediateAggs &aggs) {
        try {
            auto builder = theta_union_custom::builder().set_lg_k(lg_k).set_seed(seed);
            aggs.getIntRef(0) = reinterpret_cast<uint64_t>(new theta_union_custom(builder.build()));
        } catch (exception &e) {
            vt_report_error(0, "Exception while initializing intermediate aggregates: [%s]", e.what());
        }
    }

    void aggregate(ServerInterface &srvInterface,
                   BlockReader &argReader,
                   IntermediateAggs &aggs) {
        try {
            auto union_ptr = reinterpret_cast<theta_union_custom*>(aggs.getIntRef(0));
            do {
                auto sketch = compact_theta_sketch_custom::deserialize(argReader.getStringRef(0).data(),
                                                           argReader.getStringRef(0).length(),
                                                           seed);
                union_ptr->update(sketch);
            } while (argReader.next());
        } catch (exception &e) {
            vt_report_error(0, "Exception while processing aggregate: [%s]", e.what());
        }
    }

    // most probably, this will not work across different nodes
    // since the aggregation state is not a part of IntermediateAggs object
    virtual void combine(ServerInterface &srvInterface,
                         IntermediateAggs &aggs,
                         MultipleIntermediateAggs &aggsOther) override {
        try {
            auto union_ptr = reinterpret_cast<theta_union_custom*>(aggs.getIntRef(0));
            do {
                auto sketch = reinterpret_cast<theta_union_custom*>(aggsOther.getIntRef(0))->get_result();
                union_ptr->update(sketch);
            } while (aggsOther.next());
        } catch (exception &e) {
            vt_report_error(0, "Exception while processing aggregate: [%s]", e.what());
        }
    }

    void terminate(ServerInterface &srvInterface,
                   BlockWriter &resWriter,
                   IntermediateAggs &aggs) override {
        try {
            auto union_ptr = reinterpret_cast<theta_union_custom*>(aggs.getIntRef(0));
            auto bytes = union_ptr->get_result().serialize();
            delete union_ptr;
            VString &result = resWriter.getStringRef();
            result.copy(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        } catch (exception &e) {
            vt_report_error(0, "Exception while computing aggregate output: [%s]", e.what());
        }
    }

    InlineAggregate()
};


class ThetaSketchAggregateUnionFactory : public ThetaSketchAggregateFunctionFactory {
    virtual void getPrototype(ServerInterface &srvfloaterface, ColumnTypes &argTypes, ColumnTypes &returnType) {
        argTypes.addVarbinary();
        returnType.addVarbinary();
    }

    virtual AggregateFunction *createAggregateFunction(ServerInterface &srvfloaterface) {
        return vt_createFuncObject<ThetaSketchAggregateUnion>(srvfloaterface.allocator);
    }
};

RegisterFactory(ThetaSketchAggregateUnionFactory);

