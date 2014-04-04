/*
 Copyright (C) 2010-2014 Kristian Duske
 
 This file is part of TrenchBroom.
 
 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "TextureManager.h"

#include "Exceptions.h"
#include "CollectionUtils.h"
#include "Logger.h"
#include "Assets/Texture.h"
#include "Assets/TextureCollection.h"
#include "Assets/TextureCollectionSpec.h"
#include "Model/Game.h"

namespace TrenchBroom {
    namespace Assets {
        class CompareByName {
        public:
            CompareByName() {}
            bool operator() (const Texture* left, const Texture* right) const {
                return left->name() < right->name();
            }
        };
        
        class CompareByUsage {
        public:
            bool operator() (const Texture* left, const Texture* right) const {
                if (left->usageCount() == right->usageCount())
                    return left->name() < right->name();
                return left->usageCount() > right->usageCount();
            }
        };

        TextureManager::TextureManager(Logger* logger) :
        m_logger(logger) {}

        TextureManager::~TextureManager() {
            clear();
        }

        void TextureManager::setBuiltinTextureCollections(const IO::Path::List& paths) {
            clearBuiltinTextureCollections();
            
            TextureCollectionList newCollections;
            TextureCollectionMap newCollectionsByName;
            
            try {
                IO::Path::List::const_iterator it, end;
                for (it = paths.begin(), end = paths.end(); it != end; ++it) {
                    const IO::Path& path = *it;
                    const TextureCollectionSpec spec(path.suffix(2).asString(), path);
                    doAddTextureCollection(spec, newCollections, newCollectionsByName);
                }
                m_builtinCollections = newCollections;
                m_builtinCollectionsByName = newCollectionsByName;
                updateTextures();
            } catch (...) {
                updateTextures();
                VectorUtils::deleteAll(newCollections);
                throw;
            }
        }

        bool TextureManager::addExternalTextureCollection(const TextureCollectionSpec& spec) {
            assert(m_game != NULL);
            try {
                doAddTextureCollection(spec, m_externalCollections, m_externalCollectionsByName);
                updateTextures();
                return true;
            } catch (...) {
                TextureCollection* dummy = new TextureCollection(spec.name());
                m_externalCollections.push_back(dummy);
                m_externalCollectionsByName[spec.name()] = dummy;
                updateTextures();
                return false;
            }
        }

        void TextureManager::removeExternalTextureCollection(const String& name) {
            doRemoveTextureCollection(name, m_externalCollections, m_externalCollectionsByName, m_toRemove);
            updateTextures();
        }
        
        void TextureManager::moveExternalTextureCollectionUp(const String& name) {
            assert(m_game != NULL);
            TextureCollectionMap::iterator it = m_externalCollectionsByName.find(name);
            if (it == m_externalCollectionsByName.end())
                throw AssetException("Unknown external texture collection: '" + name + "'");
            
            TextureCollection* collection = it->second;
            const size_t index = VectorUtils::indexOf(m_externalCollections, collection);
            if (index == 0)
                throw AssetException("Could not move texture collection");
            
            using std::swap;
            swap(m_externalCollections[index-1], m_externalCollections[index]);
            updateTextures();
        }
    
        void TextureManager::moveExternalTextureCollectionDown(const String& name) {
            assert(m_game != NULL);
            TextureCollectionMap::iterator it = m_externalCollectionsByName.find(name);
            if (it == m_externalCollectionsByName.end())
                throw AssetException("Unknown external texture collection: '" + name + "'");
            
            TextureCollection* collection = it->second;
            const size_t index = VectorUtils::indexOf(m_externalCollections, collection);
            if (index == m_externalCollections.size() - 1)
                throw AssetException("Could not move texture collection");
            
            using std::swap;
            swap(m_externalCollections[index+1], m_externalCollections[index]);
            updateTextures();
        }

        void TextureManager::reset(Model::GamePtr game) {
            clear();
            m_game = game;
            updateTextures();
        }
        
        void TextureManager::commitChanges() {
            MapUtils::clearAndDelete(m_toRemove);
        }

        Texture* TextureManager::texture(const String& name) const {
            TextureMap::const_iterator it = m_texturesByName.find(name);
            if (it == m_texturesByName.end())
                return NULL;
            return it->second;
        }

        const TextureList& TextureManager::textures(const SortOrder sortOrder) const {
            return m_sortedTextures[sortOrder];
        }

        const TextureManager::GroupList& TextureManager::groups(const SortOrder sortOrder) const {
            return m_sortedGroups[sortOrder];
        }

        const TextureCollectionList& TextureManager::collections() const {
            return m_allCollections;
        }

        const StringList TextureManager::externalCollectionNames() const {
            StringList names;
            names.reserve(m_externalCollections.size());
            
            TextureCollectionList::const_iterator it, end;
            for (it = m_externalCollections.begin(), end = m_externalCollections.end(); it != end; ++it) {
                const TextureCollection* collection = *it;
                names.push_back(collection->name());
            }
            
            return names;
        }

        void TextureManager::doAddTextureCollection(const TextureCollectionSpec& spec, TextureCollectionList& collections, TextureCollectionMap& collectionsByName) {
            if (collectionsByName.find(spec.name()) == collectionsByName.end()) {
                TextureCollection* collection = m_game->loadTextureCollection(spec);
                collections.push_back(collection);
                collectionsByName.insert(std::make_pair(spec.name(), collection));
                
                if (m_logger != NULL)
                    m_logger->debug("Added texture collection %s", spec.name().c_str());
            }
        }

        void TextureManager::doRemoveTextureCollection(const String& name, TextureCollectionList& collections, TextureCollectionMap& collectionsByName, TextureCollectionMap& toRemove) {
            TextureCollectionMap::iterator it = collectionsByName.find(name);
            if (it == collectionsByName.end())
                throw AssetException("Unknown external texture collection: '" + name + "'");
            
            TextureCollection* collection = it->second;
            VectorUtils::remove(collections, collection);
            
            collectionsByName.erase(it);
            toRemove.insert(TextureCollectionMapEntry(name, collection));
            
            if (m_logger != NULL)
                m_logger->debug("Removed texture collection %s", name.c_str());
        }

        void TextureManager::clear() {
            VectorUtils::clearAndDelete(m_builtinCollections);
            VectorUtils::clearAndDelete(m_externalCollections);
            MapUtils::clearAndDelete(m_toRemove);

            if (m_logger != NULL)
                m_logger->debug("Cleared texture collections");
        }
        
        void TextureManager::clearBuiltinTextureCollections() {
            m_toRemove.insert(m_builtinCollectionsByName.begin(), m_builtinCollectionsByName.end());
            m_builtinCollections.clear();
            m_builtinCollectionsByName.clear();
            
            if (m_logger != NULL)
                m_logger->debug("Cleared builtin texture collections");
        }
        
        void TextureManager::clearExternalTextureCollections() {
            m_toRemove.insert(m_externalCollectionsByName.begin(), m_externalCollectionsByName.end());
            m_externalCollections.clear();
            m_externalCollectionsByName.clear();
            
            if (m_logger != NULL)
                m_logger->debug("Cleared builtin texture collections");
        }

        void TextureManager::updateTextures() {
            m_allCollections = VectorUtils::concatenate(m_builtinCollections, m_externalCollections);
            m_texturesByName.clear();
            m_sortedGroups[Name].clear();
            m_sortedGroups[Usage].clear();
            
            TextureCollectionList::iterator cIt, cEnd;
            for (cIt = m_allCollections.begin(), cEnd = m_allCollections.end(); cIt != cEnd; ++cIt) {
                TextureCollection* collection = *cIt;
                const TextureList textures = collection->textures();
                
                TextureList::const_iterator tIt, tEnd;
                for (tIt = textures.begin(), tEnd = textures.end(); tIt != tEnd; ++tIt) {
                    Texture* texture = *tIt;
                    texture->setOverridden(false);
                    
                    TextureMap::iterator mIt = m_texturesByName.find(texture->name());
                    if (mIt != m_texturesByName.end()) {
                        mIt->second->setOverridden(true);
                        mIt->second = texture;
                    } else {
                        m_texturesByName.insert(std::make_pair(texture->name(), texture));
                    }
                }

                const Group group = std::make_pair(collection, textures);
                m_sortedGroups[Name].push_back(group);
                m_sortedGroups[Usage].push_back(group);
                std::sort(m_sortedGroups[Name].back().second.begin(),
                          m_sortedGroups[Name].back().second.end(),
                          CompareByName());
                std::sort(m_sortedGroups[Usage].back().second.begin(),
                          m_sortedGroups[Usage].back().second.end(),
                          CompareByUsage());
            }
            
            m_sortedTextures[Name] = m_sortedTextures[Usage] = textureList();
            std::sort(m_sortedTextures[Name].begin(), m_sortedTextures[Name].end(), CompareByName());
            std::sort(m_sortedTextures[Usage].begin(), m_sortedTextures[Usage].end(), CompareByUsage());
        }

        TextureList TextureManager::textureList() const {
            TextureList result;
            TextureCollectionList::const_iterator cIt, cEnd;
            for (cIt = m_allCollections.begin(), cEnd = m_allCollections.end(); cIt != cEnd; ++cIt) {
                const TextureCollection* collection = *cIt;
                const TextureList textures = collection->textures();
                
                TextureList::const_iterator tIt, tEnd;
                for (tIt = textures.begin(), tEnd = textures.end(); tIt != tEnd; ++tIt) {
                    Texture* texture = *tIt;
                    result.push_back(texture);
                }
            }
            
            return result;
        }
    }
}
