

#include <dirent.h>
#include "ogr_source_datasets.h"
#include "configuration.h"
#include "ogr_source_util.h"

std::vector<std::string> OGRSourceDatasets::getDatasetNames(){
    const std::string path = Configuration::get<std::string>("ogrsource.files.path");

    struct dirent *entry;
    DIR *dir = opendir(path.c_str());

    if (dir == nullptr) {
        throw MustNotHappenException("OGR Source Datasets: Directory for OGR files could not be found -> " + path);
    }

    std::vector<std::string> filenames;
    std::string suffix(".json");
    size_t suffix_length = suffix.length();

    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;

        //check if file ends with the suffix
        bool found = filename.find(suffix, filename.length() - suffix_length)  != std::string::npos;

        if (found) {
            std::string dataSetName = filename.substr(0, filename.length() - suffix_length);
            filenames.push_back(dataSetName);
        }
    }

    closedir(dir);

    return filenames;
}
#include <iostream>
Json::Value OGRSourceDatasets::getDatasetListing(const std::string &dataset_name){
    Json::Value desc = getDatasetDescription(dataset_name);
    Json::Value root;
    Json::Value columns = desc["columns"];

    bool isCsv = OGRSourceUtil::hasSuffix(desc["filename"].asString(), ".csv") || OGRSourceUtil::hasSuffix(desc["filename"].asString(), ".tsv");

    Json::Value layer_array(Json::ValueType::arrayValue);

    GDALAllRegister();
    GDALDataset *dataset = OGRSourceUtil::openGDALDataset(desc);

    if(dataset == nullptr){
        throw OperatorException("OGR Source Datasets: Can not load dataset");
    }

    const int layer_count = dataset->GetLayerCount();

    for(int i = 0; i < layer_count; i++){
        OGRLayer *layer = dataset->GetLayer(i);

        std::string layer_name(layer->GetName());
        Json::Value layer_object(Json::ValueType::objectValue);
        layer_object["name"] = layer_name;

        Json::Value textual(Json::ValueType::arrayValue);
        Json::Value numeric(Json::ValueType::arrayValue);

        OGRFeatureDefn *attributeDefn = layer->GetLayerDefn();

        for(int j = 0; j < attributeDefn->GetFieldCount(); j++){
            OGRFieldDefn *field = attributeDefn->GetFieldDefn(j);
            std::string field_name  = field->GetNameRef();

            //make sure not to add geometry columns if it is csv file
            if(isCsv)
            {
                if(field_name == columns["x"].asString() ||
                   columns.isMember("y") && field_name == columns["y"].asString())
                {
                    continue;
                }
            }

            //don't add time columns
            if(columns.isMember("time1") && field_name == columns["time1"].asString() ||
               columns.isMember("time2") && field_name == columns["time2"].asString())
            {
                continue;
            }

            OGRFieldType type = field->GetType();

            //TODO: what to do with different types for the attributes, eg time? Right now it's added as textual
            if(type == OFTInteger || type == OFTInteger64 || type == OFTReal)
                numeric.append(field_name);
            else
                textual.append(field_name);
        }

        layer_object["textual"] = textual;
        layer_object["numeric"] = numeric;
        layer_array.append(layer_object);
    }

    root["layer"] = layer_array;

    GDALClose(dataset);

    return root;
}

Json::Value OGRSourceDatasets::getDatasetDescription(const std::string &name){
    std::string directory = Configuration::get<std::string>("ogrsource.files.path");
    std::string filename = directory + "/" + name + ".json";

    std::ifstream file(filename);
    if (!file.is_open()) {
        throw ArgumentException("OGR Source Datasets: File with given name not found -> " + name);
    }

    Json::Reader reader(Json::Features::strictMode());
    Json::Value root;
    bool success = reader.parse(file, root);
    if (!success) {
        throw ArgumentException("OGR Source Datasets: invalid json file");
    }

    return root;
}
