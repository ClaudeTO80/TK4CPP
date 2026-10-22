#pragma once
#include "TK4CPP/Math/Random/Generators.h"
#include <memory>
#include <string>

//#include "Poco/PBKDF2Engine.h"
//#include "Poco/HMACEngine.h"
//#include "Poco/SHA2Engine.h"

namespace TK4CPP
{
	namespace Utility
	{
		/**
		 * @class HashHandler
		 * @brief A utility class for handling hashing operations.
		 */
		class HashHandler
		{
		public:
			/**
			 * @brief Creates a hash for the given text.
			 * @param text The text to hash.
			 * @return A pair containing the generated hash and the salt used.
			 */
			static std::pair<std::string, std::string> createHash(const std::string& text);

			/**
			 * @brief Creates a hash for the given text using the provided salt.
			 * @param text The text to hash.
			 * @param salt The salt to use for hashing.
			 * @return A pair containing the generated hash and the salt used.
			 */
			static std::pair<std::string, std::string> createHash(const std::string& text, const std::string& salt);

		private:
			/**
			 * @brief Array of alphanumeric characters used for salt generation.
			 */
			static const char alphanum[];

			/**
			 * @brief Generates a random salt of the specified length.
			 * @param len The length of the salt to generate.
			 * @return The generated salt.
			 */
			static std::string generateSalt(size_t len);

			/*static void setSalting(const std::string& salt);
			static std::shared_ptr<Poco::PBKDF2Engine<Poco::HMACEngine<Poco::SHA2Engine512>>> getEngine();*/
			//static std::shared_ptr<Poco::PBKDF2Engine<Poco::HMACEngine<Poco::SHA2Engine512>>> m_pbkdf2;

			//static std::string m_salting;
			static 	std::shared_ptr<UniformIntRandomGenerator> m_rdm;

		};
	}
}