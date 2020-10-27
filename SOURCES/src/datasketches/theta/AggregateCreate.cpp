#include "Vertica.h"
#include <iostream>
#include <theta_sketch.hpp>
#include <theta_union.hpp>
#include "../../../include/datasketches/theta/theta_common.hpp"

using namespace Vertica;
using namespace std;

class ThetaSketchAggregateCreate : public AggregateFunction {
 private:
    uint8_t lg_k;
    uint64_t seed;

public:
    void setup(ServerInterface &srvInterface, const SizedColumnTypes &argTypes) {
        lg_k = readLogK(srvInterface);
        seed = readSeed(srvInterface);
    }

   // use an update_theta_sketch as the state of the aggregation
   // store a pointer in the IntermediateAggs
   void initAggregate(ServerInterface &srvInterface, IntermediateAggs &aggs) {
        try {
            auto builder = update_theta_sketch_custom::builder().set_lg_k(lg_k).set_seed(seed);
            aggs.getIntRef(0) = reinterpret_cast<uint64_t>(new update_theta_sketch_custom(builder.build()));
        } catch (exception &e) {
            vt_report_error(0, "Exception while initializing intermediate aggregates: [%s]", e.what());
        }
    }

    void aggregate(ServerInterface &srvInterface,
                   BlockReader &argReader,
                   IntermediateAggs &aggs) {
        try {
            auto sketch_ptr = reinterpret_cast<update_theta_sketch_custom*>(aggs.getIntRef(0));
            do {
                sketch_ptr->update(argReader.getStringRef(0).str());
            } while (argReader.next());
        } catch (exception &e) {
            vt_report_error(0, "Exception while processing aggregate: [%s]", e.what());
        }
    }

    // most probably, this will not work across different nodes
    // since the aggregation state is not a part of IntermediateAggs object
    void combine(ServerInterface &srvInterface,
                 IntermediateAggs &aggs,
                 MultipleIntermediateAggs &aggsOther) override {
        try {
            auto u = theta_union_custom::builder().set_lg_k(lg_k).set_seed(seed).build();
            auto sketch_ptr = reinterpret_cast<update_theta_sketch_custom*>(aggs.getIntRef(0));
            u.update(*sketch_ptr);
            do {
                auto other_sketch_ptr = reinterpret_cast<update_theta_sketch_custom*>(aggsOther.getIntRef(0));
                u.update(*other_sketch_ptr);
            } while (aggsOther.next());
            // replace the state: update_theta_sketch -> compact_theta_sketch
            // it is impossible to have an update_theta_sketch as a result of combining
            delete sketch_ptr;
            aggs.getIntRef(0) = reinterpret_cast<uint64_t>(new compact_theta_sketch_custom(u.get_result()));
        } catch (exception &e) {
            vt_report_error(0, "Exception while combining intermediate aggregates: [%s]", e.what());
        }
    }

    // this assumes that combine() is allways called (seems to be the case in limited testing)
    // expects a compact_theta_sketch as a result of combine()
    void terminate(ServerInterface &srvInterface,
                   BlockWriter &resWriter,
                   IntermediateAggs &aggs) override {
        try {
            auto sketch_ptr = reinterpret_cast<compact_theta_sketch_custom*>(aggs.getIntRef(0));
            auto bytes = sketch_ptr->serialize();
            delete sketch_ptr;
            VString &result = resWriter.getStringRef();
            result.copy(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        } catch (exception &e) {
            vt_report_error(0, "Exception while computing aggregate output: [%s]", e.what());
        }
    }

    InlineAggregate()
};


class ThetaSketchAggregateCreateVarcharFactory : public ThetaSketchAggregateFunctionFactory {
    virtual void getPrototype(ServerInterface &srvfloaterface, ColumnTypes &argTypes, ColumnTypes &returnType) {
        argTypes.addVarchar();
        returnType.addVarbinary();
    }

    virtual AggregateFunction *createAggregateFunction(ServerInterface &srvfloaterface) {
        return vt_createFuncObject<ThetaSketchAggregateCreate>(srvfloaterface.allocator);
    }
};


class ThetaSketchAggregateCreateVarbinaryFactory : public ThetaSketchAggregateFunctionFactory {
    virtual void getPrototype(ServerInterface &srvfloaterface, ColumnTypes &argTypes, ColumnTypes &returnType) {
        argTypes.addVarbinary();
        returnType.addVarbinary();
    }

    virtual AggregateFunction *createAggregateFunction(ServerInterface &srvfloaterface) {
        return vt_createFuncObject<ThetaSketchAggregateCreate>(srvfloaterface.allocator);
    }
};

RegisterFactory(ThetaSketchAggregateCreateVarcharFactory);
RegisterFactory(ThetaSketchAggregateCreateVarbinaryFactory);

RegisterLibrary(
    "Criteo",// author
    "", // lib_build_tag
    "0.1",// lib_version
    "9.2.1",// lib_sdk_version
    "https://github.com/criteo/vertica-datasketch", // URL
    "Wrapper around incubator-datasketches-cpp to make it usable in Vertica", // description
    "", // licenses required
    ""  // signature
);
