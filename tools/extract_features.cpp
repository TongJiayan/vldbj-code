#include <stdio.h>  // for snprintf
#include <string>
#include <vector>
#include <stdio.h>

#include "boost/algorithm/string.hpp"
#include "google/protobuf/text_format.h"
#include "leveldb/db.h"
#include "leveldb/write_batch.h"

#include "caffe/blob.hpp"
#include "caffe/common.hpp"
#include "caffe/net.hpp"
#include "caffe/proto/caffe.pb.h"
#include "caffe/util/io.hpp"
#include "caffe/vision_layers.hpp"

using namespace caffe;  // NOLINT(build/namespaces)

template<typename Dtype>
int feature_extraction_pipeline(int argc, char** argv);

int main(int argc, char** argv) {
  return feature_extraction_pipeline<float>(argc, argv);
//  return feature_extraction_pipeline<double>(argc, argv);
}

template<typename Dtype>
int feature_extraction_pipeline(int argc, char** argv) {
  ::google::InitGoogleLogging(argv[0]);
  const int num_required_args = 6;
  if (argc < num_required_args) {
    LOG(ERROR)<<
    "This program takes in a trained network and an input data layer, and then"
    " extract features of the input data produced by the net.\n"
    "Usage: extract_features  pretrained_net_param"
    "  feature_extraction_proto_file  extract_feature_blob_name1[,name2,...]"
    "  save_feature_leveldb_name1[,name2,...]  num_mini_batches  [CPU/GPU]"
    "  [DEVICE_ID=0] phase [Test/Train]\n"
    "Note: you can extract multiple features in one pass by specifying"
    " multiple feature blob names and leveldb names seperated by ','."
    " The names cannot contain white space characters and the number of blobs"
    " and leveldbs must be equal.";
    return 1;
  }
  int arg_pos = num_required_args;

  arg_pos = num_required_args;
  if (argc > arg_pos && strcmp(argv[arg_pos], "GPU") == 0) {
    LOG(ERROR)<< "Using GPU";
    uint device_id = 0;
    if (argc > arg_pos + 1) {
      device_id = atoi(argv[arg_pos + 1]);
      CHECK_GE(device_id, 0);
    }
    LOG(ERROR) << "Using Device_id=" << device_id;
    Caffe::SetDevice(device_id);
    Caffe::set_mode(Caffe::GPU);
  } else {
    LOG(ERROR) << "Using CPU";
    Caffe::set_mode(Caffe::CPU);
  }
  if (argc>arg_pos+1 && strcmp(argv[arg_pos+1], "Train")==0)
    Caffe::set_phase(Caffe::TRAIN);
  else 
    Caffe::set_phase(Caffe::TEST);

  arg_pos = 0;  // the name of the executable
  string pretrained_binary_proto(argv[++arg_pos]);

  // Expected prototxt contains at least one data layer such as
  //  the layer data_layer_name and one feature blob such as the
  //  fc7 top blob to extract features.
  /*
   layers {
     name: "data_layer_name"
     type: DATA
     data_param {
       source: "/path/to/your/images/to/extract/feature/images_leveldb"
       mean_file: "/path/to/your/image_mean.binaryproto"
       batch_size: 128
       crop_size: 227
       mirror: false
     }
     top: "data_blob_name"
     top: "label_blob_name"
   }
   layers {
     name: "drop7"
     type: DROPOUT
     dropout_param {
       dropout_ratio: 0.5
     }
     bottom: "fc7"
     top: "fc7"
   }
   */
  string feature_extraction_proto(argv[++arg_pos]);
  shared_ptr<Net<Dtype> > feature_extraction_net(
      new Net<Dtype>(feature_extraction_proto));
  feature_extraction_net->CopyTrainedLayersFrom(pretrained_binary_proto);

  string extract_feature_blob_names(argv[++arg_pos]);
  vector<string> blob_names;
  boost::split(blob_names, extract_feature_blob_names, boost::is_any_of(","));

  string save_feature_leveldb_names(argv[++arg_pos]);
  vector<string> leveldb_names;
  boost::split(leveldb_names, save_feature_leveldb_names,
               boost::is_any_of(","));
  CHECK_EQ(blob_names.size(), leveldb_names.size()) <<
      " the number of blob names and leveldb names must be equal";
  size_t num_features = blob_names.size();

  for (size_t i = 0; i < num_features; i++) {
    CHECK(feature_extraction_net->has_blob(blob_names[i]))
        << "Unknown feature blob name " << blob_names[i]
        << " in the network " << feature_extraction_proto;
  }

  vector<FILE*> fout;
  for (size_t i = 0; i < num_features; ++i) {
    LOG(INFO)<< "Opening leveldb " << leveldb_names[i];
    FILE* out=fopen(leveldb_names[i].c_str(), "w");
    fout.push_back(out);
  }

  int num_mini_batches = atoi(argv[++arg_pos]);

  LOG(ERROR)<< "Extacting Features";

  vector<Blob<float>*> input_vec;
  vector<int> image_indices(num_features, 0);
  for (int batch_index = 0; batch_index < num_mini_batches; ++batch_index) {
    feature_extraction_net->Forward(input_vec);
    for (int i = 0; i < num_features; ++i) {
      const shared_ptr<Blob<Dtype> > feature_blob = feature_extraction_net
          ->blob_by_name(blob_names[i]);
      int batch_size = feature_blob->num();
      int dim_features = feature_blob->count() / batch_size;
      Dtype* feature_blob_data;
      for (int n = 0; n < batch_size; ++n) {
        feature_blob_data = feature_blob->mutable_cpu_data() +
            feature_blob->offset(n);
        for (int d = 0; d < dim_features; ++d) {
          fprintf(fout[i], "%f ", feature_blob_data[d]);
        }
        fprintf(fout[i], "\n");
        string value;
        ++image_indices[i];
        if (image_indices[i] % 1000 == 0) {
          LOG(ERROR)<< "Extracted features of " << image_indices[i] <<
              " query images for feature blob " << blob_names[i];
          fflush(fout[i]);
        }
      }  // for (int n = 0; n < batch_size; ++n)
    }  // for (int i = 0; i < num_features; ++i)
  }  // for (int batch_index = 0; batch_index < num_mini_batches; ++batch_index)
  // write the last batch
  for (int i = 0; i < num_features; ++i) {
    if (image_indices[i] % 1000 != 0) {
      fflush(fout[i]);
      fclose(fout[i]);
    }
    LOG(ERROR)<< "Extracted features of " << image_indices[i] <<
        " query images for feature blob " << blob_names[i];
  }

  LOG(ERROR)<< "Successfully extracted the features!";
  return 0;
}

