
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
     * Opens the datasets json file and returns json content. This content are basic query parameters.
     * @param name of the dataset
     * @return json defining the OGR dataset
     */
    static Json::Value getDatasetDescription(const std::string &name);

    /**
     * Open the description for the dataset and append that json by the available layers in
     * the vector file on disk. Therefore the vector file is opened. Additionally for every layer the
     * numeric and textual attribtues of that layer are provided.
     * @param dataset_name
     * @return dataset description json appended by information about available layers.
     */
    static Json::Value getDatasetListing(const std::string &dataset_name);
};

#endif //MAPPING_CORE_OGR_SOURCE_DATASETS_H
