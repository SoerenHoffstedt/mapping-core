
#ifndef MAPPING_CORE_OGR_SOURCE_DATASETS_H
#define MAPPING_CORE_OGR_SOURCE_DATASETS_H

#include <json/json.h>

/**
 * This class provides information about available OGR datasets that can be opened with the OGRSourceOperator.
 * The datasets are defined in json files on disk. The location of the json files can be found in the
 * ogrsource.files.path configuration parameter.
 */
class OGRSourceDatasets {
public:
    /**
     * @return vector of names of available OGR dataset
     */
    static std::vector<std::string> getDatasetNames();

    /**
     * Opens the datasets json file and returns its content as a json object. The content is the basic query
     * parameters for opening the corresponding vector file with the OGR Source Operator.
     * @param name: name of the dataset to open.
     * @return json object defining the OGR dataset
     */
    static Json::Value getDatasetDescription(const std::string &name);

    /**
     * Returns a json object containing all the available layers in the vector file on disk.
     * Therefore the vector file is opened with OGR. Additionally for every layer the numeric
     * and textual attributes of that layer are provided.
     * @param dataset_name: name of the dataset to open.
     * @return dataset description json appended by information about available layers.
     */
    static Json::Value getDatasetListing(const std::string &dataset_name);
};

#endif //MAPPING_CORE_OGR_SOURCE_DATASETS_H
