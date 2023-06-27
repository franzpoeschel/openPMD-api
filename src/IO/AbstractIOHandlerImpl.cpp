#include "openPMD/IO/AbstractIOHandlerImpl.hpp"
#include "openPMD/backend/Writable.hpp"

#include <iostream>

namespace openPMD
{
std::future<void> AbstractIOHandlerImpl::flush()
{
    using namespace auxiliary;

    while (!(*m_handler).m_work.empty())
    {
        IOTask &i = (*m_handler).m_work.front();
        try
        {
            switch (i.operation)
            {
                using O = Operation;
            case O::CREATE_FILE: {
                auto &parameter = deref_dynamic_cast<Parameter<O::CREATE_FILE>>(
                    i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] CREATE_FILE" << std::endl;
                createFile(i.writable, parameter);
                break;
            }
            case O::CHECK_FILE: {
                auto &parameter = deref_dynamic_cast<Parameter<O::CHECK_FILE>>(
                    i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] CHECK_FILE" << std::endl;
                checkFile(i.writable, parameter);
                break;
            }
            case O::CREATE_PATH: {
                auto &parameter = deref_dynamic_cast<Parameter<O::CREATE_PATH>>(
                    i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //  << i.writable
                //  << "] CREATE_PATH: " << parameter.path
                //<< std::endl;
                createPath(i.writable, parameter);
                break;
            }
            case O::CREATE_DATASET: {
                auto &parameter =
                    deref_dynamic_cast<Parameter<O::CREATE_DATASET>>(
                        i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] CREATE_DATASET" << std::endl;
                createDataset(i.writable, parameter);
                break;
            }
            case O::EXTEND_DATASET: {
                auto &parameter =
                    deref_dynamic_cast<Parameter<O::EXTEND_DATASET>>(
                        i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] EXTEND_DATASET" << std::endl;
                extendDataset(i.writable, parameter);
                break;
            }
            case O::OPEN_FILE: {
                auto &parameter = deref_dynamic_cast<Parameter<O::OPEN_FILE>>(
                    i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] OPEN_FILE" << std::endl;
                openFile(i.writable, parameter);
                break;
            }
            case O::CLOSE_FILE: {
                auto &parameter = deref_dynamic_cast<Parameter<O::CLOSE_FILE>>(
                    i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] CLOSE_FILE" << std::endl;
                closeFile(i.writable, parameter);
                break;
            }
            case O::OPEN_PATH: {
                auto &parameter = deref_dynamic_cast<Parameter<O::OPEN_PATH>>(
                    i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] OPEN_PATH" << std::endl;
                openPath(i.writable, parameter);
                break;
            }
            case O::CLOSE_PATH: {
                auto &parameter = deref_dynamic_cast<Parameter<O::CLOSE_PATH>>(
                    i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] CLOSE_PATH" << std::endl;
                closePath(i.writable, parameter);
                break;
            }
            case O::OPEN_DATASET: {
                auto &parameter =
                    deref_dynamic_cast<Parameter<O::OPEN_DATASET>>(
                        i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] OPEN_DATASET" << std::endl;
                openDataset(i.writable, parameter);
                break;
            }
            case O::DELETE_FILE: {
                auto &parameter = deref_dynamic_cast<Parameter<O::DELETE_FILE>>(
                    i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] DELETE_FILE" << std::endl;
                deleteFile(i.writable, parameter);
                break;
            }
            case O::DELETE_PATH: {
                auto &parameter = deref_dynamic_cast<Parameter<O::DELETE_PATH>>(
                    i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] DELETE_PATH" << std::endl;
                deletePath(i.writable, parameter);
                break;
            }
            case O::DELETE_DATASET: {
                auto &parameter =
                    deref_dynamic_cast<Parameter<O::DELETE_DATASET>>(
                        i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] DELETE_DATASET" << std::endl;
                deleteDataset(i.writable, parameter);
                break;
            }
            case O::DELETE_ATT: {
                auto &parameter = deref_dynamic_cast<Parameter<O::DELETE_ATT>>(
                    i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] DELETE_ATT" << std::endl;
                deleteAttribute(i.writable, parameter);
                break;
            }
            case O::WRITE_DATASET: {
                auto &parameter =
                    deref_dynamic_cast<Parameter<O::WRITE_DATASET>>(
                        i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] WRITE_DATASET" << std::endl;
                writeDataset(i.writable, parameter);
                break;
            }
            case O::WRITE_ATT: {
                auto &parameter = deref_dynamic_cast<Parameter<O::WRITE_ATT>>(
                    i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] WRITE_ATT" << std::endl;
                writeAttribute(i.writable, parameter);
                break;
            }
            case O::READ_DATASET: {
                auto &parameter =
                    deref_dynamic_cast<Parameter<O::READ_DATASET>>(
                        i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] READ_DATASET" << std::endl;
                readDataset(i.writable, parameter);
                break;
            }
            case O::GET_BUFFER_VIEW: {
                auto &parameter =
                    deref_dynamic_cast<Parameter<O::GET_BUFFER_VIEW>>(
                        i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] GET_BUFFER_VIEW" << std::endl;
                getBufferView(i.writable, parameter);
                break;
            }
            case O::READ_ATT: {
                auto &parameter = deref_dynamic_cast<Parameter<O::READ_ATT>>(
                    i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] READ_ATT" << std::endl;
                readAttribute(i.writable, parameter);
                break;
            }
            case O::LIST_PATHS: {
                auto &parameter = deref_dynamic_cast<Parameter<O::LIST_PATHS>>(
                    i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] LIST_PATHS" << std::endl;
                listPaths(i.writable, parameter);
                break;
            }
            case O::LIST_DATASETS: {
                auto &parameter =
                    deref_dynamic_cast<Parameter<O::LIST_DATASETS>>(
                        i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] LIST_DATASETS" << std::endl;
                listDatasets(i.writable, parameter);
                break;
            }
            case O::LIST_ATTS: {
                auto &parameter = deref_dynamic_cast<Parameter<O::LIST_ATTS>>(
                    i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] LIST_ATTS" << std::endl;
                listAttributes(i.writable, parameter);
                break;
            }
            case O::ADVANCE: {
                auto &parameter = deref_dynamic_cast<Parameter<O::ADVANCE>>(
                    i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] ADVANCE" << std::endl;
                advance(i.writable, parameter);
                break;
            }
            case O::AVAILABLE_CHUNKS: {
                auto &parameter =
                    deref_dynamic_cast<Parameter<O::AVAILABLE_CHUNKS>>(
                        i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] AVAILABLE_CHUNKS"
                //<< std::endl;
                availableChunks(i.writable, parameter);
                break;
            }
            case O::DEREGISTER: {
                auto &parameter = deref_dynamic_cast<Parameter<O::DEREGISTER>>(
                    i.parameter.get());
                // std::cout << "[" << i.writable->parent << "->"
                //<< i.writable << "] DEREGISTER" << std::endl;
                deregister(i.writable, parameter);
                break;
            }
            }
        }
        catch (...)
        {
            std::cerr << "[AbstractIOHandlerImpl] IO Task "
                      << internal::operationAsString(i.operation)
                      << " failed with exception. Clearing IO queue and "
                         "passing on the exception."
                      << std::endl;
            while (!m_handler->m_work.empty())
            {
                m_handler->m_work.pop();
            }
            throw;
        }
        (*m_handler).m_work.pop();
    }
    return std::future<void>();
}
} // namespace openPMD
