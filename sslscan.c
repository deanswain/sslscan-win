/***************************************************************************
 *   sslscan - A SSL cipher scanning tool                                  *
 *   Copyright 2007-2010 by Ian Ventura-Whiting (fizz@titania.co.uk)       *
 *                          Michael Boman (michael@michaelboman.org)       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.  *
 *                                                                         *
 *   In addition, as a special exception, the copyright holders give       *
 *   permission to link the code of portions of this program with the      *
 *   OpenSSL library under certain conditions as described in each         *
 *   individual source file, and distribute linked combinations            *
 *   including the two.                                                    *
 *   You must obey the GNU General Public License in all respects          *
 *   for all of the code used other than OpenSSL.  If you modify           *
 *   file(s) with this exception, you may extend this exception to your    *
 *   version of the file(s), but you are not obligated to do so.  If you   *
 *   do not wish to do so, delete this exception statement from your       *
 *   version.  If you delete this exception statement from all source      *
 *   files in the program, then also delete it here.                       *
 ***************************************************************************/

// Includes...
#include <string.h>
#if defined(WIN32)
#include <stdio.h>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#define snprintf(...) _snprintf(__VA_ARGS__)
DWORD dwError;
#else
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>
#endif
#include <sys/stat.h>
#include <time.h>

#if defined (WIN32)
#define CLOSESOCKET(s) closesocket(s)
#else
#define CLOSESOCKET(s) close(s)
#endif

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/pkcs12.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#if defined(WIN32)
#include <openssl/applink.c>
#endif

// Defines...
#define false 0
#define true 1

#define mode_help 0
#define mode_version 1
#define mode_single 2
#define mode_multiple 3

#define BUFFERSIZE 1024

#define ssl_all 0
#define ssl_v2 1
#define ssl_v3 2
#define tls_v1 3

// Colour Console Output...
#if !defined(WIN32)
const char *RESET = "[0m";			// DEFAULT
const char *COL_RED = "[31m";		// RED
const char *COL_BLUE = "[34m";		// BLUE
const char *COL_GREEN = "[32m";	// GREEN
#else
const char *RESET = "";
const char *COL_RED = "";
const char *COL_BLUE = "";
const char *COL_GREEN = "";
#endif

#define SSLSCAN_VERSION "1.9.0-win"

const char *program_banner = "                   _\n"
                             "           ___ ___| |___  ___ __ _ _ __\n"
                             "          / __/ __| / __|/ __/ _` | '_ \\\n"
                             "          \\__ \\__ \\ \\__ \\ (_| (_| | | | |\n"
                             "          |___/___/_|___/\\___\\__,_|_| |_|\n\n"
                             "                  Version " SSLSCAN_VERSION "\n"
                             "             http://www.titania.co.uk\n"
                             " Copyright 2010 Ian Ventura-Whiting / Michael Boman\n"
							 "    Compiled against " OPENSSL_VERSION_TEXT "\n";

const char *program_version = "sslscan version " SSLSCAN_VERSION "\n" OPENSSL_VERSION_TEXT "\nhttp://www.titania.co.uk\nCopyright (C) Ian Ventura-Whiting 2009\n";
const char *xml_version = SSLSCAN_VERSION;

struct certificateOutput
{
	int version;
	long serial;
	char signatureAlgorithm[BUFFERSIZE];
	char issuer[BUFFERSIZE];
	char notValidBefore[BUFFERSIZE];
	char notValidAfter[BUFFERSIZE];
	char subject[BUFFERSIZE];
	char pkAlgorithm[BUFFERSIZE];
	char pk[BUFFERSIZE];
	char pkType[BUFFERSIZE];
	int pkError;
	int pkBits;
	struct certificateExtensionOutput *firstCertExt;
};

struct certificateExtensionOutput
{
	char name[BUFFERSIZE];
	int  level;
	char data[BUFFERSIZE];
	struct certificateExtensionOutput *next;
};

struct cipherOutput
{
	char status[BUFFERSIZE];
	char sslVersion[BUFFERSIZE];
	char cipherName[BUFFERSIZE];
	int  cipherBits;
	char cipherKx[BUFFERSIZE];
	char cipherAu[BUFFERSIZE];
	char cipherEnc[BUFFERSIZE];
	char cipherMac[BUFFERSIZE];
	char L4String[BUFFERSIZE];
};

struct sslCipher
{
	// Cipher Properties...
	const char *name;
	char *version;
	int bits;
	char description[512];
	SSL_METHOD *sslMethod;
	struct sslCipher *next;
};

struct sslCheckOptions
{
	// Program Options...
	char host[512];
	int port;
	int noFailed;
	int starttls;
	int sslVersion;
	int targets;
	int pout;
	int sslbugs;
	int http;
	int quiet;

	// File Handles...
	FILE *xmlOutput;

	// TCP Connection Variables...
	struct hostent *hostStruct;
	struct sockaddr_in serverAddress;

	// SSL Variables...
	SSL_CTX *ctx;
	struct sslCipher *ciphers;
	char *clientCertsFile;
	char *privateKeyFile;
	char *privateKeyPassword;
};

int outputCertExtension( struct sslCheckOptions *options, struct certificateExtensionOutput *outputData )
{
	if (options->quiet == false)
	{
		printf("%s: %s\n", outputData->name, outputData->level ? "critical":"");
		printf("%s\n", outputData->data);
	}

	if (options->pout)
	{
	}

	return true;
}

int outputCertExtensionXML( struct sslCheckOptions *options, struct certificateExtensionOutput *outputData )
{
	if (options->xmlOutput != 0)
	{
		fprintf(options->xmlOutput, "    <extension name=\"%s\" %s><![CDATA[%s]]></extension>\n",
			outputData->name,
			outputData->level ? "level=\"critical\"":"",
			outputData->data);
		fflush(options->xmlOutput);
	}
	return true;
}

int outputCertificate( struct sslCheckOptions *options, struct certificateOutput *outputData )
{
	if ( options->quiet == false )
	{
		printf("\n  %sSSL Certificate:%s\n", COL_BLUE, RESET);
		printf("    Version: %d\n", outputData->version);
		printf("    Serial Number: %ul\n", outputData->serial);
		printf("    Signature Algorithm: %s\n", outputData->signatureAlgorithm);
		printf("    Issuer: %s\n", outputData->issuer);
		printf("    Not valid before: %s\n", outputData->notValidBefore);
		printf("    Not valid after: %s\n", outputData->notValidAfter);
		printf("    Subject: %s\n", outputData->subject);
		printf("    Public Key Algorithm: %s\n", outputData->pkAlgorithm);
		printf("    %s Public Key: (%d bit):\n%s\n", outputData->pkAlgorithm, outputData->pkBits, outputData->pk);
		//for extension in extensions, do...
		outputCertExtension(options, outputData->firstCertExt);
	}

	if ( options->xmlOutput != 0 )
	{
		fprintf(options->xmlOutput, "  <certificate>\n");
		fprintf(options->xmlOutput, "   <version>%d</version>\n", outputData->version);
		fprintf(options->xmlOutput, "   <serial>%l</serial>\n", outputData->serial);
		fprintf(options->xmlOutput, "   <signature-algorithm>%s</signature-algorithm>\n", outputData->signatureAlgorithm);
		fprintf(options->xmlOutput, "   <issuer>%s</issuer>\n", outputData->issuer);
		fprintf(options->xmlOutput, "   <not-valid-before>%s</not-valid-before>\n", outputData->notValidBefore);
		fprintf(options->xmlOutput, "   <not-valid-after>%s</not-valid-after>\n", outputData->notValidAfter);
		fprintf(options->xmlOutput, "   <subject>%s</subject>\n", outputData->subject);
		fprintf(options->xmlOutput, "   <pk-algorithm>%s</pk-algorithm>\n", outputData->pkAlgorithm);
		fprintf(options->xmlOutput, "   <pk error=\"%s\" type=\"%s\" bits=\"%d\">\n%s\n   </pk>",
			outputData->pkError ? "true":"false",
			outputData->pkType,
			outputData->pkBits,
			outputData->pk);
		fprintf(options->xmlOutput, "   <X509v3-Extensions>\n");
		//for extension in extensions, do...
		outputCertExtensionXML(options, outputData->firstCertExt);
		fprintf(options->xmlOutput, "   </X509v3-Extensions>\n");
		fprintf(options->xmlOutput, "  </certificate>\n");
		fflush(options->xmlOutput);
	}

	return true;
}

int outputCipher( struct sslCheckOptions *options, struct cipherOutput *outputData )
{
	if ((strcmp(outputData->status, "accepted") == 0) || // Status is accepted
		(options->noFailed == false) // Include failed ciphers
		)
	{
		if (options->quiet == false)
		{
			printf("    %-8s  %s  %3d bits  %s\n",
				outputData->status,
				outputData->sslVersion,
				outputData->cipherBits,
				outputData->cipherName
				);
		}

		if (options->pout)
		{
			printf("|| %s || %s || %3d || %s ||\n",
				outputData->status,
				outputData->sslVersion,
				outputData->cipherBits,
				outputData->cipherName
				);
		}

		if (options->xmlOutput != 0)
		{
			fprintf(options->xmlOutput, "  <cipher status=\"%s\" sslversion=\"%s\" bits=\"%d\" cipher=\"%s\" kx=\"%s\" au=\"%s\" enc=\"%s\" mac=\"%s\" />\n",
				outputData->status,
				outputData->sslVersion,
				outputData->cipherBits, 
				outputData->cipherName,
				outputData->cipherKx,
				outputData->cipherAu,
				outputData->cipherEnc,
				outputData->cipherMac);
			fflush(options->xmlOutput);
		}
	}
	return true;
}

int outputPreferedCipher( struct sslCheckOptions *options, struct cipherOutput *outputData )
{
	if (strcmp(outputData->status, "accepted") == 0)
	{
		if (options->quiet == false)
		{
			printf("    %s  %3d bits  %s\n",
				outputData->sslVersion,
				outputData->cipherBits,
				outputData->cipherName
				);
		}

		if (options->pout)
		{
			printf("|| %s || %3d || %s ||\n",
				outputData->sslVersion,
				outputData->cipherBits,
				outputData->cipherName
				);
		}

		if (options->xmlOutput != 0)
		{
			fprintf(options->xmlOutput, "  <defaultcipher sslversion=\"%s\" bits=\"%d\" cipher=\"%s\" kx=\"%s\" au=\"%s\" enc=\"%s\" mac=\"%s\" />\n",
				outputData->sslVersion,
				outputData->cipherBits, 
				outputData->cipherName,
				outputData->cipherKx,
				outputData->cipherAu,
				outputData->cipherEnc,
				outputData->cipherMac);
			fflush(options->xmlOutput);
		}
	}
	return true;
}


int parseDescription( char *description, struct cipherOutput *dest )
{
	sscanf(description, "%*s %*s Kx=%s Au=%s Enc=%s Mac=%s",
		dest->cipherKx,
		dest->cipherAu,
		dest->cipherEnc,
		dest->cipherMac);
	return true;
}

void set_blocking(SSL * ssl)
{
#if defined (WIN32)
	int fd, res;
	u_long iMode = 0;
	if( (fd = SSL_get_rfd(ssl)) )       
	{ 
		res = ioctlsocket(fd, FIONBIO, &iMode);
		if( res )
		{
			// Something went wrong...
			dwError = WSAGetLastError();
			if (dwError != 0) {
					fprintf(stderr, "%sERROR in set_blocking(): %ld.%s\n", COL_RED, dwError, RESET);
			}
		}
	}
#else
  int fd, flags;      

  /* SSL_get_rfd returns -1 on error */
  if( (fd = SSL_get_rfd(ssl)) )       
  { 
	flags = fcntl(fd, F_GETFL);
    flags &= ~O_NONBLOCK;
    fcntl(fd, F_SETFL, flags);
  } 

  /* SSL_get_wfd returns -1 on error */  
  if( (fd = SSL_get_wfd(ssl)) )      
  {
    flags = fcntl(fd, F_GETFL);
    flags &= ~O_NONBLOCK;
    fcntl(fd, F_SETFL, flags);
  }
#endif
}

// Adds Ciphers to the Cipher List structure
int populateCipherList(struct sslCheckOptions *options, SSL_METHOD *sslMethod)
{
	// Variables...
	int returnCode = true;
	struct sslCipher *sslCipherPointer;
	int tempInt;
	int loop;
	STACK_OF(SSL_CIPHER) *cipherList;
	SSL *ssl = NULL;

	// Setup Context Object...
	options->ctx = SSL_CTX_new(sslMethod);
	if (options->ctx != NULL)
	{
		SSL_CTX_set_cipher_list(options->ctx, "ALL:COMPLEMENTOFALL");

		// Create new SSL object
		ssl = SSL_new(options->ctx);
		if (ssl != NULL)
		{
			// Get List of Ciphers
			cipherList = SSL_get_ciphers(ssl);
	
			// Create Cipher Struct Entries...
			for (loop = 0; loop < sk_SSL_CIPHER_num(cipherList); loop++)
			{
				// Create Structure...
				if (options->ciphers == 0)
				{
					options->ciphers = malloc(sizeof(struct sslCipher));
					sslCipherPointer = options->ciphers;
				}
				else
				{
					sslCipherPointer = options->ciphers;
					while (sslCipherPointer->next != 0)
						sslCipherPointer = sslCipherPointer->next;
					sslCipherPointer->next = malloc(sizeof(struct sslCipher));
					sslCipherPointer = sslCipherPointer->next;
				}
	
				// Init
				memset(sslCipherPointer, 0, sizeof(struct sslCipher));
	
				// Add cipher information...
				sslCipherPointer->sslMethod = sslMethod;
				sslCipherPointer->name = SSL_CIPHER_get_name(sk_SSL_CIPHER_value(cipherList, loop));
				sslCipherPointer->version = SSL_CIPHER_get_version(sk_SSL_CIPHER_value(cipherList, loop));
				SSL_CIPHER_description(sk_SSL_CIPHER_value(cipherList, loop), sslCipherPointer->description, sizeof(sslCipherPointer->description) - 1);
				sslCipherPointer->bits = SSL_CIPHER_get_bits(sk_SSL_CIPHER_value(cipherList, loop), &tempInt);
			}
	
			// Free SSL object
			SSL_free(ssl);
		}
		else
		{
			returnCode = false;
			fprintf(stderr, "%sERROR: Could not create SSL object.%s\n", COL_RED, RESET);
		}

		// Free CTX Object
		SSL_CTX_free(options->ctx);
	}

	// Error Creating Context Object
	else
	{
		returnCode = false;
		fprintf(stderr, "%sERROR: Could not create CTX object.%s\n", COL_RED, RESET);
	}

	return returnCode;
}


// File Exists
int fileExists(char *fileName)
{
	// Variables...
	struct stat fileStats;

	if (stat(fileName, &fileStats) == 0)
		return true;
	else
		return false;
}


// Read a line from the input...
void readLine(FILE *input, char *lineFromFile, int maxSize)
{
	// Variables...
	int stripPointer;

	// Read line from file...
	fgets(lineFromFile, maxSize, input);

	// Clear the end-of-line stuff...
	stripPointer = strlen(lineFromFile) -1;
	while ((lineFromFile[stripPointer] == '\r') || (lineFromFile[stripPointer] == '\n') || (lineFromFile[stripPointer] == ' '))
	{
		lineFromFile[stripPointer] = 0;
		stripPointer--;
	}
}


// Create a TCP socket
int tcpConnect(struct sslCheckOptions *options)
{
	// Variables...
	int socketDescriptor;
	char buffer[BUFFERSIZE];
	struct sockaddr_in localAddress;
	int status;

	// Create Socket
	socketDescriptor = socket(AF_INET, SOCK_STREAM, 0);
	if(socketDescriptor < 0)
	{
		fprintf(stderr, "%s    ERROR: Could not open a socket.%s\n", COL_RED, RESET);
		return 0;
	}

	// Configure Local Port
	localAddress.sin_family = AF_INET;
	localAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	localAddress.sin_port = htons(0);
	status = bind(socketDescriptor, (struct sockaddr *) &localAddress, sizeof(localAddress));
	if(status < 0)
	{
		fprintf(stderr, "%s    ERROR: Could not bind to port.%s\n", COL_RED, RESET);
		return 0;
	}

	// Connect
	status = connect(socketDescriptor, (struct sockaddr *) &options->serverAddress, sizeof(options->serverAddress));
	if(status < 0)
	{
		fprintf(stderr, "%s    ERROR: Could not open a connection to host %s on port %d.%s\n", COL_RED, options->host, options->port, RESET);
		return 0;
	}

	// If STARTTLS is required...
	if (options->starttls == true)
	{
		memset(buffer, 0, BUFFERSIZE);
		recv(socketDescriptor, buffer, BUFFERSIZE - 1, 0);
		if (strncmp(buffer, "220", 3) != 0)
		{
			CLOSESOCKET(socketDescriptor);
			fprintf(stderr, "%s    ERROR: The host %s on port %d did not appear to be an SMTP service.%s\n", COL_RED, options->host, options->port, RESET);
			return 0;
		}
		send(socketDescriptor, "EHLO titania.co.uk\r\n", 20, 0);
		memset(buffer, 0, BUFFERSIZE);
		recv(socketDescriptor, buffer, BUFFERSIZE - 1, 0);
		if (strncmp(buffer, "250", 3) != 0)
		{
			CLOSESOCKET(socketDescriptor);
			fprintf(stderr, "%s    ERROR: The SMTP service on %s port %d did not respond with status 250 to our HELO.%s\n", COL_RED, options->host, options->port, RESET);
			return 0;
		}
		send(socketDescriptor, "STARTTLS\r\n", 10, 0);
		memset(buffer, 0, BUFFERSIZE);
		recv(socketDescriptor, buffer, BUFFERSIZE - 1, 0);
		if (strncmp(buffer, "220", 3) != 0)
		{
			CLOSESOCKET(socketDescriptor);
			fprintf(stderr, "%s    ERROR: The SMTP service on %s port %d did not appear to support STARTTLS.%s\n", COL_RED, options->host, options->port, RESET);
			return 0;
		}
	}

	// Return
	return socketDescriptor;
}


// Private Key Password Callback...
static int password_callback(char *buf, int size, int rwflag, void *userdata)
{
	strncpy(buf, (char *)userdata, size);
	buf[strlen(userdata)] = 0;
	return strlen(userdata);
}


// Load client certificates/private keys...
int loadCerts(struct sslCheckOptions *options)
{
	// Variables...
	int status = 1;
	PKCS12 *pk12 = NULL;
	FILE *pk12File = NULL;
	X509 *cert = NULL;
	EVP_PKEY *pkey = NULL;
	STACK_OF(X509) *ca = NULL;

	// Configure PKey password...
	if (options->privateKeyPassword != 0)
	{
		SSL_CTX_set_default_passwd_cb_userdata(options->ctx, (void *)options->privateKeyPassword);
		SSL_CTX_set_default_passwd_cb(options->ctx, password_callback);
	}

	// Seperate Certs and PKey Files...
	if ((options->clientCertsFile != 0) && (options->privateKeyFile != 0))
	{
		// Load Cert...
		if (!SSL_CTX_use_certificate_file(options->ctx, options->clientCertsFile, SSL_FILETYPE_PEM))
		{
			if (!SSL_CTX_use_certificate_file(options->ctx, options->clientCertsFile, SSL_FILETYPE_ASN1))
			{
				if (!SSL_CTX_use_certificate_chain_file(options->ctx, options->clientCertsFile))
				{
					fprintf(stderr, "%s    Could not configure certificate(s).%s\n", COL_RED, RESET);
					status = 0;
				}
			}
		}

		// Load PKey...
		if (status != 0)
		{
			if (!SSL_CTX_use_PrivateKey_file(options->ctx, options->privateKeyFile, SSL_FILETYPE_PEM))
			{
				if (!SSL_CTX_use_PrivateKey_file(options->ctx, options->privateKeyFile, SSL_FILETYPE_ASN1))
				{
					if (!SSL_CTX_use_RSAPrivateKey_file(options->ctx, options->privateKeyFile, SSL_FILETYPE_PEM))
					{
						if (!SSL_CTX_use_RSAPrivateKey_file(options->ctx, options->privateKeyFile, SSL_FILETYPE_ASN1))
						{
							fprintf(stderr, "%s    Could not configure private key.%s\n", COL_RED, RESET);
							status = 0;
						}
					}
				}
			}
		}
	}

	// PKCS Cert and PKey File...
	else if (options->privateKeyFile != 0)
	{
		pk12File = fopen(options->privateKeyFile, "rb");
		if (pk12File != NULL)
		{
			pk12 = d2i_PKCS12_fp(pk12File, NULL);
			if (!pk12)
			{
				status = 0;
				fprintf(stderr, "%s    Could not read PKCS#12 file.%s\n", COL_RED, RESET);
			}
			else
			{
				if (!PKCS12_parse(pk12, options->privateKeyPassword, &pkey, &cert, &ca))
				{
					status = 0;
					fprintf(stderr, "%s    Error parsing PKCS#12. Are you sure that password was correct?%s\n", COL_RED, RESET);
				}
				else
				{
					if (!SSL_CTX_use_certificate(options->ctx, cert))
					{
						status = 0;
						fprintf(stderr, "%s    Could not configure certificate.%s\n", COL_RED, RESET);
					}
					if (!SSL_CTX_use_PrivateKey(options->ctx, pkey))
					{
						status = 0;
						fprintf(stderr, "%s    Could not configure private key.%s\n", COL_RED, RESET);
					}
				}
				PKCS12_free(pk12);
			}
			fclose(pk12File);
		}
		else
		{
			fprintf(stderr, "%s    Could not open PKCS#12 file.%s\n", COL_RED, RESET);
			status = 0;
		}
	}

	// Check Cert/Key...
	if (status != 0)
	{
		if (!SSL_CTX_check_private_key(options->ctx))
		{
			fprintf(stderr, "%s    Private key does not match certificate.%s\n", COL_RED, RESET);
			return false;
		}
		else
			return true;
	}
	else
		return false;
}


// Test a cipher...
int testCipher(struct sslCheckOptions *options, struct sslCipher *sslCipherPointer)
{
	// Variables...
	int cipherStatus;
	int status = true;
	int socketDescriptor = 0;
	SSL *ssl = NULL;
	BIO *cipherConnectionBio;
	BIO *stdoutBIO = NULL;
	char requestBuffer[BUFFERSIZE];
	char buffer[50];
	int resultSize = 0;
	struct cipherOutput *myCipherOutput = NULL;
	myCipherOutput = malloc(sizeof(struct cipherOutput));

	// Create request buffer...
	memset(requestBuffer, 0, BUFFERSIZE);
	snprintf(requestBuffer, BUFFERSIZE-1, "GET / HTTP/1.0\r\nUser-Agent: SSLScan\r\nHost: %s\r\n\r\n", options->host);
	// Connect to host
	socketDescriptor = tcpConnect(options);
	if (socketDescriptor != 0)
	{
		if (SSL_CTX_set_cipher_list(options->ctx, sslCipherPointer->name) != 0)
		{

			// Create SSL object...
			ssl = SSL_new(options->ctx);
			if (ssl != NULL)
			{
				// Connect socket and BIO
				cipherConnectionBio = BIO_new_socket(socketDescriptor, BIO_NOCLOSE);

				// Connect SSL and BIO
				SSL_set_bio(ssl, cipherConnectionBio, cipherConnectionBio);

				// Connect SSL over socket
				cipherStatus = SSL_connect(ssl);

				// Show Cipher Status
				if (!((options->noFailed == true) && (cipherStatus != 1)))
				{
					if (cipherStatus == 1)
					{
						sprintf(myCipherOutput->status, "accepted"); 
						if (options->http == true)
						{
							// Stdout BIO...
							stdoutBIO = BIO_new(BIO_s_file());
							BIO_set_fp(stdoutBIO, stdout, BIO_NOCLOSE);

							// HTTP Get...
							SSL_write(ssl, requestBuffer, sizeof(requestBuffer));
							memset(buffer ,0 , 50);
							resultSize = SSL_read(ssl, buffer, 49);
							if (resultSize > 9)
							{
								int loop = 0;
								for (loop = 9; (loop < 49) && (buffer[loop] != 0) && (buffer[loop] != '\r') && (buffer[loop] != '\n'); loop++)
								{ }
								buffer[loop] = 0;

								// Output HTTP code...
								sprintf(myCipherOutput->L4String, "%s", buffer + 9);
							}
						}
					}
					else if (cipherStatus == 0)
					{
						sprintf(myCipherOutput->status, "rejected");
						if (options->http == true)
						{
							sprintf(myCipherOutput->L4String, "Rejected");
						}
					}
					else
					{
						sprintf(myCipherOutput->status, "failed");
						if (options->http == true)
						{
							sprintf(myCipherOutput->L4String, "Failed");
						}
					}

					if (sslCipherPointer->sslMethod == SSLv2_client_method())
					{
						sprintf(myCipherOutput->sslVersion, "SSLv2");
					}
					else if (sslCipherPointer->sslMethod == SSLv3_client_method())
					{
						sprintf(myCipherOutput->sslVersion, "SSLv3");
					}
					else if (sslCipherPointer->sslMethod == TLSv1_client_method())
					{
						sprintf(myCipherOutput->sslVersion, "TLSv1");
					}
					else
					{
						sprintf(myCipherOutput->sslVersion, "Unknown");
					}

					myCipherOutput->cipherBits = sslCipherPointer->bits;
					parseDescription(sslCipherPointer->description, myCipherOutput);
					sprintf(myCipherOutput->cipherName, "%s", sslCipherPointer->name);
				}

				// Disconnect SSL over socket
				if (cipherStatus == 1)
					SSL_shutdown(ssl);

				// Free SSL object
				SSL_free(ssl);
			}
			else
			{
				status = false;
				fprintf(stderr, "%s    ERROR: Could create SSL object.%s\n", COL_RED, RESET);
			}
		}
		else
		{
			status = false;
			fprintf(stderr, "%s    ERROR: Could set cipher %s.%s\n", COL_RED, sslCipherPointer->name, RESET);
		}

		// Disconnect from host
		CLOSESOCKET(socketDescriptor);
	}

	// Could not connect
	else
		status = false;

	outputCipher( options, myCipherOutput );

	if (myCipherOutput != NULL)
		free(myCipherOutput);

	return status;
}

// Check if the server supports renegotiation
int testRenegotiation(struct sslCheckOptions *options, SSL_METHOD *sslMethod)
{
	// Variables...
	int cipherStatus;
	int status = true;
	int secure = false;
	int socketDescriptor = 0;
	int res;
	SSL *ssl = NULL;
	BIO *cipherConnectionBio;

	// Connect to host
	socketDescriptor = tcpConnect(options);
	if (socketDescriptor != 0)
	{

		// Setup Context Object...
		options->ctx = SSL_CTX_new(sslMethod);
		if (options->ctx != NULL)
		{
			if (SSL_CTX_set_cipher_list(options->ctx, "ALL:COMPLEMENTOFALL") != 0)
			{

				// Load Certs if required...
				if ((options->clientCertsFile != 0) || (options->privateKeyFile != 0))
					status = loadCerts(options);

				if (status == true)
				{
					// Create SSL object...
					ssl = SSL_new(options->ctx);
#if ( OPENSSL_VERSION_NUMBER > 0x009080cfL )
					// Make sure we can connect to insecure servers
					// OpenSSL is going to change the default at a later date
					SSL_set_options(ssl, SSL_OP_LEGACY_SERVER_CONNECT); 
#endif
					if (ssl != NULL)
					{
						// Connect socket and BIO
						cipherConnectionBio = BIO_new_socket(socketDescriptor, BIO_NOCLOSE);

						// Connect SSL and BIO
						SSL_set_bio(ssl, cipherConnectionBio, cipherConnectionBio);

						// Connect SSL over socket
						cipherStatus = SSL_connect(ssl);

						if (cipherStatus == 1)
						{
#if ( OPENSSL_VERSION_NUMBER > 0x009080cfL )
							// SSL_get_secure_renegotiation_support() appeared first in OpenSSL 0.9.8m
							secure = SSL_get_secure_renegotiation_support(ssl);
							if( secure )
							{
								// If it supports secure renegotiations,
								// it should have renegotioation support in general
								status = true;
								if (options->quiet == false)
									printf("\n\nSecure renegotiation supported\n");
							}
							else
							{
#endif
								// We can't assume that just because the secure renegotiation
								// support failed the server doesn't support insecure renegotiations 

								// assume ssl is connected and error free up to here
								set_blocking(ssl); // this is unnecessary if it is already blocking 
								SSL_renegotiate(ssl); // Ask to renegotiate the connection
								SSL_do_handshake(ssl); // Send renegotiation request to server

								if (ssl->state == SSL_ST_OK)
								{
									//ssl->state |= SSL_ST_ACCEPT;
									//ssl->state = SSL_ST_ACCEPT;
									res = SSL_do_handshake(ssl); // Send renegotiation request to server
									if( res != 1 )
									{
										fprintf(stderr, "\n\nSSL_do_handshake() call failed\n");
									}
									if (ssl->state == SSL_ST_OK)
									{
										/* our renegotiation is complete */
										status = true;
										if (options->quiet == false)
											printf("\n\nRenegotiation requests supported\n");
									} else {
										status = false;
										fprintf(stderr, "\n\nFailed to complete renegotiation\n");
									}
								} else {
									status = false;
									if (options->quiet == false)
										printf("\n\nRenegotiation requests not supported\n");
								}
#if ( OPENSSL_VERSION_NUMBER > 0x009080cfL )
							}
#endif
							// Disconnect SSL over socket
							SSL_shutdown(ssl);
						}

						// Free SSL object
						SSL_free(ssl);
					}
					else
					{
						status = false;
						fprintf(stderr, "%s    ERROR: Could create SSL object.%s\n", COL_RED, RESET);
					}
				}
			}
			else
			{
				status = false;
				fprintf(stderr, "%s    ERROR: Could set cipher.%s\n", COL_RED, RESET);
			}
			
			// Free CTX Object
			SSL_CTX_free(options->ctx);
		}
	
		// Error Creating Context Object
		else
		{
			status = false;
			fprintf(stderr, "%sERROR: Could not create CTX object.%s\n", COL_RED, RESET);
		}

		// Disconnect from host
		CLOSESOCKET(socketDescriptor);
	}

	// Could not connect
	else
		status = false;

	if (options->xmlOutput != 0)
		fprintf(options->xmlOutput, "  <renegotiation supported=\"%d\" secure=\"%d\" />\n", status, secure);

	return status;					

}


// Test for prefered ciphers
int defaultCipher(struct sslCheckOptions *options, SSL_METHOD *sslMethod)
{
	// Variables...
	int cipherStatus;
	int status = true;
	int socketDescriptor = 0;
	SSL *ssl = NULL;
	BIO *cipherConnectionBio;
	int tempInt2;
	struct cipherOutput *myCipherOutput = NULL;
	myCipherOutput = malloc(sizeof(struct cipherOutput));


	// Connect to host
	socketDescriptor = tcpConnect(options);
	if (socketDescriptor != 0)
	{

		// Setup Context Object...
		options->ctx = SSL_CTX_new(sslMethod);
		if (options->ctx != NULL)
		{
			if (SSL_CTX_set_cipher_list(options->ctx, "ALL:COMPLEMENTOFALL") != 0)
			{

				// Load Certs if required...
				if ((options->clientCertsFile != 0) || (options->privateKeyFile != 0))
					status = loadCerts(options);

				if (status == true)
				{
					// Create SSL object...
					ssl = SSL_new(options->ctx);
					if (ssl != NULL)
					{
						// Connect socket and BIO
						cipherConnectionBio = BIO_new_socket(socketDescriptor, BIO_NOCLOSE);

						// Connect SSL and BIO
						SSL_set_bio(ssl, cipherConnectionBio, cipherConnectionBio);

						// Connect SSL over socket
						cipherStatus = SSL_connect(ssl);
						if (cipherStatus == 1)
						{
							sprintf(myCipherOutput->status, "accepted");
							if (sslMethod == SSLv2_client_method())
							{
								sprintf(myCipherOutput->sslVersion, "SSLv2");
							}
							else if (sslMethod == SSLv3_client_method())
							{
								sprintf(myCipherOutput->sslVersion, "SSLv3");
							}
							else if (sslMethod == TLSv1_client_method())
							{
								sprintf(myCipherOutput->sslVersion, "TLSv1");
							}
							else
							{
								sprintf(myCipherOutput->sslVersion, "Unknown");
							}

							myCipherOutput->cipherBits = SSL_get_cipher_bits(ssl, &tempInt2);
							parseDescription(options->ciphers->description, myCipherOutput);
							sprintf(myCipherOutput->cipherName, "%s", SSL_get_cipher_name(ssl));

							// Disconnect SSL over socket
							SSL_shutdown(ssl);
						}
						else
						{
							sprintf(myCipherOutput->status, "error");
						}

						// Free SSL object
						SSL_free(ssl);
					}
					else
					{
						status = false;
						sprintf(myCipherOutput->status, "error");
						fprintf(stderr, "%s    ERROR: Could create SSL object.%s\n", COL_RED, RESET);
					}
				}
			}
			else
			{
				status = false;
				fprintf(stderr, "%s    ERROR: Could set cipher.%s\n", COL_RED, RESET);
			}
			
			// Free CTX Object
			SSL_CTX_free(options->ctx);
		}
	
		// Error Creating Context Object
		else
		{
			status = false;
			fprintf(stderr, "%sERROR: Could not create CTX object.%s\n", COL_RED, RESET);
		}

		// Disconnect from host
		CLOSESOCKET(socketDescriptor);
	}

	// Could not connect
	else
		status = false;

	outputPreferedCipher( options, myCipherOutput );

	if ( myCipherOutput != NULL )
		free( myCipherOutput );

	return status;
}


// Get certificate...
int getCertificate(struct sslCheckOptions *options)
{
	// Variables...
	int cipherStatus = 0;
	int status = true;
	int socketDescriptor = 0;
	SSL *ssl = NULL;
	BIO *cipherConnectionBio = NULL;
	BIO *stdoutBIO = NULL;
	BIO *fileBIO = NULL;
	X509 *x509Cert = NULL;
	EVP_PKEY *publicKey = NULL;
	SSL_METHOD *sslMethod = NULL;
	ASN1_OBJECT *asn1Object = NULL;
	X509_EXTENSION *extension = NULL;
	char buffer[1024];
	long tempLong = 0;
	int tempInt = 0;
	int tempInt2 = 0;
	long verifyError = 0;
	struct certificateOutput *myCertOutput;
	myCertOutput = malloc(sizeof( struct certificateOutput));

	// Connect to host
	socketDescriptor = tcpConnect(options);
	if (socketDescriptor != 0)
	{

		// Setup Context Object...
		sslMethod = SSLv23_method();
		options->ctx = SSL_CTX_new(sslMethod);
		if (options->ctx != NULL)
		{

			if (SSL_CTX_set_cipher_list(options->ctx, "ALL:COMPLEMENTOFALL") != 0)
			{

				// Load Certs if required...
				if ((options->clientCertsFile != 0) || (options->privateKeyFile != 0))
					status = loadCerts(options);

				if (status == true)
				{
					// Create SSL object...
					ssl = SSL_new(options->ctx);
					if (ssl != NULL)
					{

						// Connect socket and BIO
						cipherConnectionBio = BIO_new_socket(socketDescriptor, BIO_NOCLOSE);

						// Connect SSL and BIO
						SSL_set_bio(ssl, cipherConnectionBio, cipherConnectionBio);

						// Connect SSL over socket
						cipherStatus = SSL_connect(ssl);
						if (cipherStatus == 1)
						{

							// Setup BIO's
							stdoutBIO = BIO_new(BIO_s_file());
							BIO_set_fp(stdoutBIO, stdout, BIO_NOCLOSE);
							if (options->xmlOutput != 0)
							{
								fileBIO = BIO_new(BIO_s_file());
								BIO_set_fp(fileBIO, options->xmlOutput, BIO_NOCLOSE); //TODO: See http://code.google.com/p/sslscan-win/issues/detail?id=2
							}

							// Get Certificate...
							if (options->quiet == false)
								printf("\n  %sSSL Certificate:%s\n", COL_BLUE, RESET);
							if (options->xmlOutput != 0)
								fprintf(options->xmlOutput, "  <certificate>\n");
							x509Cert = SSL_get_peer_certificate(ssl);
							if (x509Cert != NULL)
							{

								//SSL_set_verify(ssl, SSL_VERIFY_NONE|SSL_VERIFY_CLIENT_ONCE, NULL);

								// Cert Version
								if (!(X509_FLAG_COMPAT & X509_FLAG_NO_VERSION))
								{
									myCertOutput->version = X509_get_version(x509Cert);

									tempLong = X509_get_version(x509Cert);
									if (options->quiet == false)
										printf("    Version: %lu\n", tempLong);
									if (options->xmlOutput != 0)
										fprintf(options->xmlOutput, "   <version>%lu</version>\n", tempLong);
								}

								// Cert Serial No.
								if (!(X509_FLAG_COMPAT & X509_FLAG_NO_SERIAL))
								{
									myCertOutput->serial = ASN1_INTEGER_get(X509_get_serialNumber(x509Cert));

									tempLong = ASN1_INTEGER_get(X509_get_serialNumber(x509Cert));
									if (tempLong < 1)
									{
										if (options->quiet == false)
											printf("    Serial Number: -%lu\n", tempLong);
										if (options->xmlOutput != 0)
											fprintf(options->xmlOutput, "   <serial>-%lu</serial>\n", tempLong);
									}
									else
									{
										if (options->quiet == false)
											printf("    Serial Number: %lu\n", tempLong);
										if (options->xmlOutput != 0)
											fprintf(options->xmlOutput, "   <serial>%lu</serial>\n", tempLong);
									}
								}

								// Signature Algo...
								if (!(X509_FLAG_COMPAT & X509_FLAG_NO_SIGNAME))
								{
									//myCertOutput->signatureAlgorithm = 

									if (options->quiet == false)
									{
										printf("    Signature Algorithm: ");
										i2a_ASN1_OBJECT(stdoutBIO, x509Cert->cert_info->signature->algorithm);
										printf("\n");
									}
									if (options->xmlOutput != 0)
									{
										fprintf(options->xmlOutput, "   <signature-algorithm>");
										i2a_ASN1_OBJECT(fileBIO, x509Cert->cert_info->signature->algorithm);
										fprintf(options->xmlOutput, "</signature-algorithm>\n");
									}
								}

								// SSL Certificate Issuer...
								if (!(X509_FLAG_COMPAT & X509_FLAG_NO_ISSUER))
								{
									X509_NAME_oneline(X509_get_issuer_name(x509Cert), buffer, sizeof(buffer) - 1);
									if (options->quiet == false)
										printf("    Issuer: %s\n", buffer);
									if (options->xmlOutput != 0)
										fprintf(options->xmlOutput, "   <issuer>%s</issuer>\n", buffer);
								}

								// Validity...
								if (!(X509_FLAG_COMPAT & X509_FLAG_NO_VALIDITY))
								{
									if (options->quiet == false)
									{
										printf("    Not valid before: ");
										ASN1_TIME_print(stdoutBIO, X509_get_notBefore(x509Cert));
										printf("\n    Not valid after: ");
										ASN1_TIME_print(stdoutBIO, X509_get_notAfter(x509Cert));
										printf("\n");
									}
									if (options->xmlOutput != 0)
									{
										fprintf(options->xmlOutput, "   <not-valid-before>");
										ASN1_TIME_print(fileBIO, X509_get_notBefore(x509Cert));
										fprintf(options->xmlOutput, "</not-valid-before>\n");
										fprintf(options->xmlOutput, "   <not-valid-after>");
										ASN1_TIME_print(fileBIO, X509_get_notAfter(x509Cert));
										fprintf(options->xmlOutput, "</not-valid-after>\n");
									}
								}

								// SSL Certificate Subject...
								if (!(X509_FLAG_COMPAT & X509_FLAG_NO_SUBJECT))
								{
									X509_NAME_oneline(X509_get_subject_name(x509Cert), buffer, sizeof(buffer) - 1);
									if (options->quiet == false)
										printf("    Subject: %s\n", buffer);
									if (options->xmlOutput != 0)
										fprintf(options->xmlOutput, "   <subject>%s</subject>\n", buffer);
								}

								// Public Key Algo...
								if (!(X509_FLAG_COMPAT & X509_FLAG_NO_PUBKEY))
								{
									if (options->quiet == false)
									{
										printf("    Public Key Algorithm: ");
										i2a_ASN1_OBJECT(stdoutBIO, x509Cert->cert_info->key->algor->algorithm);
										printf("\n");
									}
									if (options->xmlOutput != 0)
									{
										fprintf(options->xmlOutput, "   <pk-algorithm>");
										i2a_ASN1_OBJECT(fileBIO, x509Cert->cert_info->key->algor->algorithm);
										fprintf(options->xmlOutput, "</pk-algorithm>\n");
									}

									// Public Key...
									publicKey = X509_get_pubkey(x509Cert);
									if (publicKey == NULL)
									{
										if (options->quiet == false)
											printf("    Public Key: Could not load\n");
										if (options->xmlOutput != 0)
											fprintf(options->xmlOutput, "   <pk error=\"true\" />\n");
									}
									else
									{
										switch (publicKey->type)
										{
											case EVP_PKEY_RSA:
												if (options->quiet == false)
												{
													printf("    RSA Public Key: (%d bit)\n", BN_num_bits(publicKey->pkey.rsa->n));
													RSA_print(stdoutBIO, publicKey->pkey.rsa, 6);
												}
												if (options->xmlOutput != 0)
												{
													fprintf(options->xmlOutput, "   <pk error=\"false\" type=\"RSA\" bits=\"%d\">\n", BN_num_bits(publicKey->pkey.rsa->n));
													RSA_print(fileBIO, publicKey->pkey.rsa, 4);
													fprintf(options->xmlOutput, "   </pk>\n");
												}
												break;
											case EVP_PKEY_DSA:
												if (options->quiet == false)
												{
													printf("    DSA Public Key:\n");
													DSA_print(stdoutBIO, publicKey->pkey.dsa, 6);
												}
												if (options->xmlOutput != 0)
												{
													fprintf(options->xmlOutput, "   <pk error=\"false\" type=\"DSA\">\n");
													DSA_print(fileBIO, publicKey->pkey.dsa, 4);
													fprintf(options->xmlOutput, "   </pk>\n");
												}
												break;
											case EVP_PKEY_EC:
												if (options->quiet == false)
												{
													printf("    EC Public Key:\n");
													EC_KEY_print(stdoutBIO, publicKey->pkey.ec, 6);
												}
												if (options->xmlOutput != 0)
												{
													fprintf(options->xmlOutput, "   <pk error=\"false\" type=\"EC\">\n");
													EC_KEY_print(fileBIO, publicKey->pkey.ec, 4);
													fprintf(options->xmlOutput, "   </pk>\n");
												}
												break;
											default:
												if (options->quiet == false)
													printf("    Public Key: Unknown\n");
												if (options->xmlOutput != 0)
													fprintf(options->xmlOutput, "   <pk error=\"true\" type=\"unknown\" />\n");
												break;
										}

										EVP_PKEY_free(publicKey);
									}
								}

								// X509 v3...
								if (!(X509_FLAG_COMPAT & X509_FLAG_NO_EXTENSIONS))
								{
									if (sk_X509_EXTENSION_num(x509Cert->cert_info->extensions) > 0)
									{
										if (options->quiet == false)
											printf("    X509v3 Extensions:\n");
										if (options->xmlOutput != 0)
											fprintf(options->xmlOutput, "   <X509v3-Extensions>\n");
										for (tempInt = 0; tempInt < sk_X509_EXTENSION_num(x509Cert->cert_info->extensions); tempInt++)
										{
											// Get Extension...
											extension = sk_X509_EXTENSION_value(x509Cert->cert_info->extensions, tempInt);

											// Print Extension name...
											asn1Object = X509_EXTENSION_get_object(extension);
											tempInt2 = X509_EXTENSION_get_critical(extension);
											if (options->quiet == false)
											{
												printf("      ");											
												i2a_ASN1_OBJECT(stdoutBIO, asn1Object);
												BIO_printf(stdoutBIO, ": %s\n", tempInt2 ? "critical" : "");
											}
											if (options->xmlOutput != 0)
											{
												fprintf(options->xmlOutput, "    <extension name=\"");
												i2a_ASN1_OBJECT(fileBIO, asn1Object);
												BIO_printf(fileBIO, "\"%s><![CDATA[", tempInt2 ? " level=\"critical\"" : "");
											}

											// Print Extension value...
											if (options->quiet == false)
											{
												if (!X509V3_EXT_print(stdoutBIO, extension, X509_FLAG_COMPAT, 8))
												{
													printf("        ");
													M_ASN1_OCTET_STRING_print(stdoutBIO, extension->value);
													printf("\n");
												}
											}

											if (options->xmlOutput != 0)
											{
												if (!X509V3_EXT_print(fileBIO, extension, X509_FLAG_COMPAT, 0))
													M_ASN1_OCTET_STRING_print(fileBIO, extension->value);
												fprintf(options->xmlOutput, "]]></extension>\n");
											}	
										}
										if (options->xmlOutput != 0)
											fprintf(options->xmlOutput, "   </X509v3-Extensions>\n");
									}
								}

								// Verify Certificate...
								if (options->quiet == false)
								{
									printf("  Verify Certificate:\n");
									verifyError = SSL_get_verify_result(ssl);
									if (verifyError == X509_V_OK)
										printf("    Certificate passed verification\n");
									else
										printf("    %s\n", X509_verify_cert_error_string(verifyError));
								}
								// Free X509 Certificate...
								X509_free(x509Cert);
							}

							if (options->xmlOutput != 0)
								fprintf(options->xmlOutput, "  </certificate>\n");

							// Free BIO
							BIO_free(stdoutBIO);
							if (options->xmlOutput != 0)
								BIO_free(fileBIO);

							// Disconnect SSL over socket
							SSL_shutdown(ssl);
						}

						// Free SSL object
						SSL_free(ssl);
					}
					else
					{
						status = false;
						fprintf(stderr, "%s    ERROR: Could create SSL object.%s\n", COL_RED, RESET);
					}
				}
			}
			else
			{
				status = false;
				fprintf(stderr, "%s    ERROR: Could set cipher.%s\n", COL_RED, RESET);
			}

			// Free CTX Object
			SSL_CTX_free(options->ctx);
		}

		// Error Creating Context Object
		else
		{
			status = false;
			fprintf(stderr, "%sERROR: Could not create CTX object.%s\n", COL_RED, RESET);
		}

		// Disconnect from host
		CLOSESOCKET(socketDescriptor);
	}

	// Could not connect
	else
		status = false;

	return status;
}


// Test a single host and port for ciphers...
int testHost(struct sslCheckOptions *options)
{
	// Variables...
	struct sslCipher *sslCipherPointer;
	int status = true;
	time_t rawtime;
	struct tm * timeinfo;
	char datetime[BUFFERSIZE];

	// Resolve Host Name
#if defined (WIN32)
	WORD wVersionRequested;
	WSADATA wsaData;
	int err;
	wVersionRequested = MAKEWORD( 1, 1 );
	err = WSAStartup( wVersionRequested, &wsaData );
#endif

	options->hostStruct = gethostbyname(options->host);

#if defined (WIN32)
	dwError = WSAGetLastError();
	if (dwError != 0) {
		if (dwError == WSAHOST_NOT_FOUND) {
			//printf("Host not found\n");
			fprintf(stderr, "%sERROR: Could not resolve hostname %s: Host not found.%s\n", COL_RED, options->host, RESET);
			return false;
		} else if (dwError == WSANO_DATA) {
			//printf("No data record found\n");
			fprintf(stderr, "%sERROR: Could not resolve hostname %s: No data record found.%s\n", COL_RED, options->host, RESET);
			return false;
		} else {
			//printf("Function failed with error: %ld\n", dwError);
			fprintf(stderr, "%sERROR: Could not resolve hostname %s: Error(%ld).%s\n", COL_RED, options->host, dwError, RESET);
			return false;
		}
	}
#else
	if (options->hostStruct == NULL)
	{	
		fprintf(stderr, "%sERROR: Could not resolve hostname %s.%s\n", COL_RED, options->host, RESET);
		return false;
	}
#endif

	// Configure Server Address and Port
	options->serverAddress.sin_family = options->hostStruct->h_addrtype;
	memcpy((char *) &options->serverAddress.sin_addr.s_addr, options->hostStruct->h_addr_list[0], options->hostStruct->h_length);
	options->serverAddress.sin_port = htons(options->port);

	// XML Output...
	if (options->xmlOutput != 0)
	{
		time( &rawtime );
		timeinfo = gmtime( &rawtime );
		strftime( datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S +0000", timeinfo); //TODO: Output timezone as offset to GMT

		fprintf(options->xmlOutput, " <ssltest host=\"%s\" port=\"%d\" time=\"%s\">\n", options->host, options->port, datetime);
	}

	// Test supported ciphers...
	if (options->quiet == false)
	{
		printf("\n%sTesting SSL server %s on port %d%s\n\n", COL_GREEN, options->host, options->port, RESET);
		printf("  %sSupported Server Cipher(s):%s\n", COL_BLUE, RESET);
		if ((options->http == true) && (options->pout == true))
			printf("|| Status || HTTP Code || Version || Bits || Cipher ||\n");
		else if (options->pout == true)
			printf("|| Status || Version || Bits || Cipher ||\n");
	}
	sslCipherPointer = options->ciphers;
	while ((sslCipherPointer != 0) && (status == true))
	{

		// Setup Context Object...
		options->ctx = SSL_CTX_new(sslCipherPointer->sslMethod);
		if (options->ctx != NULL)
		{

			// SSL implementation bugs/workaround
			if (options->sslbugs)
				SSL_CTX_set_options(options->ctx, SSL_OP_ALL | 0);
			else
				SSL_CTX_set_options(options->ctx, 0);

			// Load Certs if required...
			if ((options->clientCertsFile != 0) || (options->privateKeyFile != 0))
				status = loadCerts(options);

			// Test
			if (status == true)
				status = testCipher(options, sslCipherPointer);

			// Free CTX Object
			SSL_CTX_free(options->ctx);
		}
	
		// Error Creating Context Object
		else
		{
			status = false;
			fprintf(stderr, "%sERROR: Could not create CTX object.%s\n", COL_RED, RESET);
		}

		sslCipherPointer = sslCipherPointer->next;
	}

	if (status == true)
	{
		// Test prefered ciphers...
		if (options->quiet == false)
		{
			printf("\n  %sPrefered Server Cipher(s):%s\n", COL_BLUE, RESET);
			if (options->pout == true)
				printf("|| Version || Bits || Cipher ||\n");
		}
		switch (options->sslVersion)
		{
			case ssl_all:
				status = defaultCipher(options, SSLv2_client_method());
				if (status != false)
					status = defaultCipher(options, SSLv3_client_method());
				if (status != false)
					status = defaultCipher(options, TLSv1_client_method());
				break;
			case ssl_v2:
				status = defaultCipher(options, SSLv2_client_method());
				break;
			case ssl_v3:
				status = defaultCipher(options, SSLv3_client_method());
				break;
			case tls_v1:
				status = defaultCipher(options, TLSv1_client_method());
				break;
		}
	}

	if (status == true)
	{
		status = getCertificate(options);
	}

	if (status == true)
	{
		status = testRenegotiation(options, TLSv1_client_method());
	}

	// XML Output...
	if (options->xmlOutput != 0)
		fprintf(options->xmlOutput, " </ssltest>\n");

	// Return status...
	return status;
}


int main(int argc, char *argv[])
{
	// Variables...
	struct sslCheckOptions options;
	struct sslCipher *sslCipherPointer;
	int status;
	int argLoop;
	int tempInt;
	int maxSize;
	int xmlArg;
	int mode = mode_help;
	FILE *targetsFile;
	char line[1024];
    
	// Init...
	memset(&options, 0, sizeof(struct sslCheckOptions));
	options.port = 443;
	xmlArg = 0;
	strcpy(options.host, "127.0.0.1");
	options.noFailed = false;
	options.starttls = false;
	options.sslVersion = ssl_all;
	options.pout = false;
	options.quiet = false;
	SSL_library_init();

	// Get program parameters
	for (argLoop = 1; argLoop < argc; argLoop++)
	{
		// Help
		if (strcmp("--help", argv[argLoop]) == 0)
			mode = mode_help;

		// targets
		else if ((strncmp("--targets=", argv[argLoop], 10) == 0) && (strlen(argv[argLoop]) > 10))
		{
			mode = mode_multiple;
			options.targets = argLoop;
		}

		// Show only supported
		else if (strcmp("--no-failed", argv[argLoop]) == 0)
			options.noFailed = true;

		// Version
		else if (strcmp("--version", argv[argLoop]) == 0)
			mode = mode_version;

		// XML Output
		else if (strncmp("--xml=", argv[argLoop], 6) == 0)
			xmlArg = argLoop;

		// P Output
		else if (strcmp("-p", argv[argLoop]) == 0)
			options.pout = true;

		// Quiet output
		else if (strcmp("--quiet", argv[argLoop]) == 0)
			options.quiet = true;

		// Client Certificates
		else if (strncmp("--certs=", argv[argLoop], 8) == 0)
			options.clientCertsFile = argv[argLoop] +8;

		// Private Key File
		else if (strncmp("--pk=", argv[argLoop], 5) == 0)
			options.privateKeyFile = argv[argLoop] +5;

		// Private Key Password
		else if (strncmp("--pkpass=", argv[argLoop], 9) == 0)
			options.privateKeyPassword = argv[argLoop] +9;

		// StartTLS...
		else if (strcmp("--starttls", argv[argLoop]) == 0)
		{
			options.sslVersion = tls_v1;
			options.starttls = true;
			options.port = 25; // default to SMTP when you want to use STARTTLS
		}

		// SSL v2 only...
		else if (strcmp("--ssl2", argv[argLoop]) == 0)
			options.sslVersion = ssl_v2;

		// SSL v3 only...
		else if (strcmp("--ssl3", argv[argLoop]) == 0)
			options.sslVersion = ssl_v3;

		// TLS v1 only...
		else if (strcmp("--tls1", argv[argLoop]) == 0)
			options.sslVersion = tls_v1;

		// SSL Bugs...
		else if (strcmp("--bugs", argv[argLoop]) == 0)
			options.sslbugs = 1;

		// SSL HTTP Get...
		else if (strcmp("--http", argv[argLoop]) == 0)
			options.http = 1;

		// Host (maybe port too)...
		else if (argLoop + 1 == argc)
		{
			mode = mode_single;

			// Get host...
			tempInt = 0;
			maxSize = strlen(argv[argLoop]);
			while ((argv[argLoop][tempInt] != 0) && (argv[argLoop][tempInt] != ':'))
				tempInt++;
			argv[argLoop][tempInt] = 0;
			strncpy(options.host, argv[argLoop], sizeof(options.host) -1);

			// Get port (if it exists)...
			tempInt++;
			if (tempInt < maxSize)
				options.port = atoi(argv[argLoop] + tempInt);
		}

		// Not too sure what the user is doing...
		else
			mode = mode_help;
	}

	// Open XML file output...
	if ((xmlArg > 0) && (mode != mode_help))
	{
		if( strcmp(argv[xmlArg] + 6, "stdout") == 0)
		{
			options.xmlOutput = stdout;
		}
		else {
			options.xmlOutput = fopen(argv[xmlArg] + 6, "w");
		}
		if (options.xmlOutput == NULL)
		{
			fprintf(stderr, "%sERROR: Could not open XML output file %s.%s\n", COL_RED, argv[xmlArg] + 6, RESET);
			exit(0);
		}
		
		// Output file header...
		fprintf(options.xmlOutput, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<document title=\"SSLScan Results\" version=\"%s\" web=\"http://www.titania.co.uk\">\n", xml_version);
	}

	switch (mode)
	{
		case mode_version:
			printf("%s%s%s", COL_BLUE, program_version, RESET);
			break;

		case mode_help:
			// Program version banner...
			printf("%s%s%s\n", COL_BLUE, program_banner, RESET);
			printf("SSLScan is a fast SSL port scanner. SSLScan connects to SSL\n");
			printf("ports and determines what  ciphers are supported, which are\n");
			printf("the servers  prefered  ciphers,  which  SSL  protocols  are\n");
			printf("supported  and   returns  the   SSL   certificate.   Client\n");
			printf("certificates /  private key can be configured and output is\n");
			printf("to text / XML.\n\n");
			printf("%sCommand:%s\n", COL_BLUE, RESET);
			printf("  %s%s [Options] [host:port | host]%s\n\n", COL_GREEN, argv[0], RESET);
			printf("%sOptions:%s\n", COL_BLUE, RESET);
			printf("  %s--targets=<file>%s     A file containing a list of hosts to\n", COL_GREEN, RESET);
			printf("                       check.  Hosts can  be supplied  with\n");
			printf("                       ports (i.e. host:port).\n");
			printf("  %s--no-failed%s          List only accepted ciphers  (default\n", COL_GREEN, RESET);
			printf("                       is to listing all ciphers).\n");
			printf("  %s--ssl2%s               Only check SSLv2 ciphers.\n", COL_GREEN, RESET);
			printf("  %s--ssl3%s               Only check SSLv3 ciphers.\n", COL_GREEN, RESET);
			printf("  %s--tls1%s               Only check TLSv1 ciphers.\n", COL_GREEN, RESET);
			printf("  %s--pk=<file>%s          A file containing the private key or\n", COL_GREEN, RESET);
			printf("                       a PKCS#12  file containing a private\n");
			printf("                       key/certificate pair (as produced by\n");
			printf("                       MSIE and Netscape).\n");
			printf("  %s--pkpass=<password>%s  The password for the private  key or\n", COL_GREEN, RESET);
			printf("                       PKCS#12 file.\n");
			printf("  %s--certs=<file>%s       A file containing PEM/ASN1 formatted\n", COL_GREEN, RESET);
			printf("                       client certificates.\n");
			printf("  %s--starttls%s           If a STARTTLS is required to kick an\n", COL_GREEN, RESET);
			printf("                       SMTP service into action.\n");
			printf("  %s--http%s               Test a HTTP connection.\n", COL_GREEN, RESET);
			printf("  %s--bugs%s               Enable SSL implementation  bug work-\n", COL_GREEN, RESET);
			printf("                       arounds.\n");
			printf("  %s--xml=<file>%s         Output results to an XML file.\n", COL_GREEN, RESET);
			printf("  %s--version%s            Display the program version.\n", COL_GREEN, RESET);
			printf("  %s--quiet%s              Be quiet\n", COL_GREEN, RESET);
			printf("  %s--help%s               Display the  help text  you are  now\n", COL_GREEN, RESET);
			printf("                       reading.\n");
			printf("%sExample:%s\n", COL_BLUE, RESET);
			printf("  %s%s 127.0.0.1%s\n\n", COL_GREEN, argv[0], RESET);
			break;

		// Check a single host/port ciphers...
		case mode_single:
		case mode_multiple:
			if (options.quiet == false)
				printf("%s%s%s", COL_BLUE, program_banner, RESET);

			SSLeay_add_all_algorithms();
			ERR_load_crypto_strings();

			// Build a list of ciphers...
			switch (options.sslVersion)
			{
				case ssl_all:
					populateCipherList(&options, SSLv2_client_method());
					populateCipherList(&options, SSLv3_client_method());
					populateCipherList(&options, TLSv1_client_method());
					break;
				case ssl_v2:
					populateCipherList(&options, SSLv2_client_method());
					break;
				case ssl_v3:
					populateCipherList(&options, SSLv3_client_method());
					break;
				case tls_v1:
					populateCipherList(&options, TLSv1_client_method());
					break;
			}

			// Do the testing...
			if (mode == mode_single)
				status = testHost(&options);
			else
			{
				if (fileExists(argv[options.targets] + 10) == true)
				{
					// Open targets file...
					targetsFile = fopen(argv[options.targets] + 10, "r");
					if (targetsFile == NULL)
						fprintf(stderr, "%sERROR: Could not open targets file %s.%s\n", COL_RED, argv[options.targets] + 10, RESET);
					else
					{
						readLine(targetsFile, line, sizeof(line));
						while (feof(targetsFile) == 0)
						{
							if (strlen(line) != 0)
							{
								// Get host...
								tempInt = 0;
								while ((line[tempInt] != 0) && (line[tempInt] != ':'))
									tempInt++;
								line[tempInt] = 0;
								strncpy(options.host, line, sizeof(options.host) -1);

								// Get port (if it exists)...
								tempInt++;
								if (strlen(line + tempInt) > 0)
									options.port = atoi(line + tempInt);

								// Test the host...
								status = testHost(&options);
							}
							readLine(targetsFile, line, sizeof(line));
						}
					}
				}
				else
					fprintf(stderr, "%sERROR: Targets file %s does not exist.%s\n", COL_RED, argv[options.targets] + 10, RESET);
			}
	
			// Free Structures
			while (options.ciphers != 0)
			{
				sslCipherPointer = options.ciphers->next;
				free(options.ciphers);
				options.ciphers = sslCipherPointer;
			}
			break;
	}

	// Close XML file, if required...
	if ((xmlArg > 0) && (mode != mode_help))
	{
		fprintf(options.xmlOutput, "</document>\n");
		fclose(options.xmlOutput);
	}

	return 0;
}

