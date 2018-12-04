#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include <Windows.h>
#include <time.h>

#define TAM_CORPO_MSG 34 // em bytes
#define TAM_HEADER_MSG 1 // em bytes
#define TAM_END_MSG 1 // em bytes
#define TAM_MSG (TAM_CORPO_MSG + TAM_HEADER_MSG + TAM_END_MSG) //em bytes
#define TAM_RESP 2 //em bytes
#define TAM_NBYTES 1 // em bytes
#define TAM_MAX_NOME_ARQV 16 // em bytes
#define TAM_CHECKSUM_ARQV 4 //em bytes
#define CHECKSUM_TYPE unsigned int
#define ANSWER_TIMEOUT 4 //in seconds
#define MAX_TIMEOUTS 3
#define BYTE_SIZE 8
#define SERIAL1 "\\\\.\\COM8"
#define READ_INTERVAL_TIMEOUT 50        //EXPLICAR DPS OQUE É CADA UMA DESSAS CONTASTANTE , NO DOCUMENTO E NO ARQUIVO FONTE
#define BAUDRATE 9600					// /\/\/\/\/\/\/\/\/\//\/\/\/\/\/\/\/\/\/\/
#define READ_TOTAL_TIMEOUT_CONST 50
#define READ_TOTAL_TIMEOUT_MULT 10
#define WRITE_TOTAL_TIMMEOUT_CONST 50
#define WRITE_TOTAL_TIMEOUT_MULT 10

//Operations
#define OP_CRIAR		0b00000000
#define OP_APAGAR		0b00000010
#define OP_VERIFICAR	0b00000100
#define OP_PROGRAMAR	0b00000110
#define OP_ENVIAR_PAC	0b00000001
#define OP_SD_BEGIN		0b00001110
#define OP_SYNC			0b00001000

//Error codes
#define ERROR_SD_BEGIN                            0b01110000
#define OK                                        0b00000000
#define ERROR_CREATE_FILE_ALREADY_EXISTS          0b00010000
#define ERROR_CREATE_OPENING_NOT_POSSIBLE         0b00100000
#define ERROR_ERASE_FILE_DOESNT_EXIST             0b00010000
#define ERROR_ERASE_ERASING_NOT_POSSIBLE          0b00100000
#define ERROR_WRITE_DATA_INVALID_OPERATION        0b00010000
#define ERROR_WRITE_DATA_OPENING_NOT_POSSIBLE     0b00100000
#define ERROR_WRITE_DATA_ALL_DATA_NOT_WRITTEN     0b00110000
#define ERROR_VERIFY_FILE_DOESNT_EXIST            0b00010000
#define ERROR_VERIFY_OPENING_NOT_POSSIBLE         0b00100000
#define ERROR_VERIFY_INCORRECT_CHECKSUM           0b00110000
#define ERROR_UPLOAD_INVALID_OPERATION            0b00010000

//Masks
#define OP_MASK                          0b00001111
#define ERROR_MASK                       0b01110000
#define ALTERNATE_BIT_MASK               0b10000000

//Variaveis Globais
byte alternateBit = 0; //bit de controle (ALTERNATE BIT) --> 0(Primeira transmissão da mensagem) | 1(Rentramissão da mensagem)
byte txMsg[TAM_MSG];
DWORD txBytes;
int timeoutCounter = 0;

//COMENTAR SOBRE CADA PARAMETRO DAS FUNÇÕES <------------------------------
void DEBUGMsg(byte msg[], int tamMsg);
HANDLE InitComm(const char serialPortNumber[]);
int Send(HANDLE commHandler, LPCVOID bytesToSend, const char  bufferSize, DWORD *bytesWritten, LPOVERLAPPED overlappedStruct);
int Receive(HANDLE commHandler, LPVOID bytesRead, int  bufferSize, DWORD *numBytesRead, LPOVERLAPPED overlappedStruct);
int CloseComm(HANDLE commHandler);
int FOTA_Operation(HANDLE commHandler, int nNo, char nomeArqv[], int numBytesMsg, const int op);
int FOTA_OperationVerificar(HANDLE commHandler, int nNo, char nomeArqv[], int numBytesMsg, const int op, unsigned int checkSum);
int FOTA_OperationSync(HANDLE commHandler, int nNo, const int op);
int FOTA_Sync(HANDLE commHandler, int nNo);
int FOTA_CriarArqv(HANDLE commHandler, int nNo, char nomeArqv[]);
int FOTA_VerificarArqv(HANDLE commHandler, int nNo, char nomeArqv[]);
int FOTA_Programar(HANDLE commHandler, int nNo, char nomeArqv[]);
int FOTA_ApagarArqv(HANDLE commHandler, int nNo, char nomeArqv[]);
int FOTA_EnviarPac(HANDLE commHandler, int nNo, byte pacote[], int numBytesPacote);
int EsperarResposta(HANDLE commHandler, byte* buffer, int tamBuffer);
unsigned int CalcularCheckSumArqv(char nomeArqv[]);
int TratarResposta(byte* resposta);
void VerifyError(byte header);
int CheckTimeOut(clock_t timer, float timeLimit);
void Menu(char* filename, int* op);
void AskForAddress(int* addr);
int Sync(HANDLE hSerial, int addr);
int UploadNewFirmware(HANDLE hSerial, char* nomeArqv, int addr);
int UploadSavedFirmware(HANDLE hSerial, char* nomeArqv, int addr);
int EraseFile(HANDLE hSerial, char* nomeArqv, int addr);
int ReceiveAnswer(HANDLE hSerial, byte* resposta);

//COLOCAR TODAS CHAMADAS DENTRO DE UM IF PARA VERIFICAR SE HOUVE ERROS DE EXECUÇÃO
//TRANSFORMAR O CÓDIGO INTEIRO EM UMA LINGUA SÓ
int main()
{
	char nomeArqv[TAM_MAX_NOME_ARQV];
	HANDLE hSerial;
	int op, addr;
	//Abertura da porta Serial
	hSerial = InitComm(SERIAL1);

	AskForAddress(&addr);

	Sync(hSerial, addr);

	do
	{
		Menu(nomeArqv, &op);

		switch (op)
		{
		case 1:
			UploadNewFirmware(hSerial, nomeArqv, addr);
			break;
		case 2:
			UploadSavedFirmware(hSerial, nomeArqv, addr);
			break;
		case 3:
			EraseFile(hSerial, nomeArqv, addr);
			break;
		case 4:
			break;
		default:
			break;
		}
	} while (op != 4);
	
	return 0;
}

//FUNÇÕES DE DEBUG---------
void DEBUGMsg(byte msg[], int tamMsg)
{
	int i;

	for (i = 0; i < tamMsg; i++)
	{
		fprintf(stderr, "%x ", msg[i]);
		
	}
    
	fprintf(stderr, "\n");
}

//---------------------------
HANDLE InitComm(const char serialPortNumber[])
{
	HANDLE commHandler;
	DCB dcbSerialParams = { 0 };
	COMMTIMEOUTS timeouts = { 0 };

	// Open the serial port with "serialPortNumber" number
	fprintf(stderr, "Iniciando Porta Serial...\n");

	commHandler = CreateFile(
		serialPortNumber, GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (commHandler == INVALID_HANDLE_VALUE)
	{
		fprintf(stderr, "Erro ao Iniciar a Porta Serial...\n");
		return INVALID_HANDLE_VALUE;
	}

	fprintf(stderr, "Porta Serial Iniciada com Sucesso...\n");


	// Set device parameters (38400 baud, 1 start bit,
	// 1 stop bit, no parity)
	dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

	if (GetCommState(commHandler, &dcbSerialParams) == 0)
	{
		fprintf(stderr, "Erro na Obtenção do Status do Dispositivo...\n");
		CloseHandle(commHandler);
		return INVALID_HANDLE_VALUE;
	}

	dcbSerialParams.BaudRate = BAUDRATE;
	dcbSerialParams.ByteSize = BYTE_SIZE;
	dcbSerialParams.StopBits = ONESTOPBIT;
	dcbSerialParams.Parity = NOPARITY;

	if ((SetCommState(commHandler, &dcbSerialParams) == 0))
	{
		fprintf(stderr, "Erro na Configuração dos Parametros...\n");
		CloseHandle(commHandler);
		return INVALID_HANDLE_VALUE;
	}

	// Set COM port timeout settings
	timeouts.ReadIntervalTimeout = READ_INTERVAL_TIMEOUT;
	timeouts.ReadTotalTimeoutConstant = READ_TOTAL_TIMEOUT_CONST;
	timeouts.ReadTotalTimeoutMultiplier = READ_TOTAL_TIMEOUT_MULT;
	timeouts.WriteTotalTimeoutConstant = WRITE_TOTAL_TIMMEOUT_CONST;
	timeouts.WriteTotalTimeoutMultiplier = WRITE_TOTAL_TIMMEOUT_CONST;

	if (SetCommTimeouts(commHandler, &timeouts) == 0)
	{
		fprintf(stderr, "Erro na Configuração dos Timeouts...\n");
		CloseHandle(commHandler);
		return INVALID_HANDLE_VALUE;
	}

	fprintf(stderr, "Porta Serial Configurada com Sucesso...\n");

	return commHandler;
}

//return 0 erro , return 1 ok
int Send(HANDLE commHandler, LPCVOID bytesToSend, const char  bufferSize, DWORD *bytesWritten, LPOVERLAPPED overlappedStruct)
{
	char foo;
	DWORD numBytesLidos = 1;

	//Clear Rx
	fprintf(stderr, "Limpando buffer Rx...\n");
	while (numBytesLidos)
	{
		Receive(commHandler, &foo, 1, &numBytesLidos, NULL);
	}
		
	// Send specified text (remaining command line arguments)
	fprintf(stderr, "Enviando Dados Pela Serial...\n");
	if (!WriteFile(commHandler, bytesToSend, bufferSize, bytesWritten, overlappedStruct))
	{
		fprintf(stderr, "Erro Durante o Envio...\n\n");
		CloseHandle(commHandler);
		return 0;
	}

	fprintf(stderr, "%d Bytes Enviados...\n", *bytesWritten);

	return 1;
}
//return 0 erro , return 1 ok
int Receive(HANDLE commHandler, LPVOID bytesRead, int  bufferSize, DWORD *numBytesRead, LPOVERLAPPED overlappedStruct)
{
	if (!ReadFile(commHandler, bytesRead, bufferSize, numBytesRead, overlappedStruct))
	{
		fprintf(stderr, "Erro Durante a Leitura da Mensagem...\n");
		CloseHandle(commHandler);
		return 0;
	}

	//DEBUG
	fprintf(stderr, "[%d]", *numBytesRead);
	

	return 1;
}
//return 0 erro , return 1 ok
int CloseComm(HANDLE commHandler)
{
	// Close serial port
	fprintf(stderr, "Fechando Portal Serial...");
	if (CloseHandle(commHandler) == 0)
	{
		fprintf(stderr, "Erro ao Fechar a Porta Serial...\n");
		return 0;
	}
	fprintf(stderr, "OK...\n");

	return 1;
}

int ResendLastMessage(HANDLE commHandler)
{
	DWORD txBytesEscritos;

	fprintf(stderr, "Resending message...");
	//DEBUG
	DEBUGMsg(txMsg, txBytes);
	return Send(commHandler, txMsg, txBytes , &txBytesEscritos, NULL);
}
//INSERIR O BIT DE VALIDADE NO HEADER
//return 0 erro , return 1 ok
int FOTA_Operation(HANDLE commHandler, int nNo, char nomeArqv[], int numBytesMsg, const int op)
{
	int i;
	DWORD txBytesEscritos;
	//log
	fprintf(stderr, ">>>> Realizando operacao: %i <<<<\n", op);
	fprintf(stderr, "Montando Mensagem...\n");

	txMsg[0] = nNo; //montagem do endereçamento da mensagem

	txMsg[TAM_END_MSG] = op | (alternateBit ? ALTERNATE_BIT_MASK : 0); //montagem do HEADER da mensagem

	//alternateBit ^= 1; //alterna o bit

	txMsg[TAM_END_MSG + TAM_HEADER_MSG] = numBytesMsg; //montagem do tamanho da mensagem

	//montagem do CORPO da  mensagem
	for (i = 0; i < numBytesMsg; i++)
	{
		txMsg[TAM_END_MSG + TAM_HEADER_MSG + TAM_NBYTES + i] = nomeArqv[i]; //inserção do nome do arquivo
	}

	//DEBUG
	DEBUGMsg(txMsg, TAM_END_MSG + TAM_HEADER_MSG + TAM_NBYTES + i);

	txBytes = TAM_END_MSG + TAM_HEADER_MSG + TAM_NBYTES + i;//define tamanho da mensagem

	//envio da mensagem
	return Send(commHandler, txMsg, txBytes, &txBytesEscritos, NULL);
	
}
//INSERIR O BIT DE VALIDADE NO HEADER
//return 0 erro , return 1 ok
int FOTA_OperationVerificar(HANDLE commHandler, int nNo, char nomeArqv[], int tamNomeArqv,  const int op, unsigned int checkSum)
{
	DWORD bytes_escritos;
	int i;

	//log
	fprintf(stderr, ">>>> Realizando operacao: %i <<<<\n", op);
	fprintf(stderr, "Montando Mensagem...\n");

	txMsg[0] = nNo; //montagem do endereçamento da mensagem

	txMsg[TAM_END_MSG] = op | (alternateBit ? ALTERNATE_BIT_MASK : 0); //montagem do HEADER da mensagem

	//alternateBit ^= 1; //alterna o bit

	txMsg[TAM_END_MSG + TAM_HEADER_MSG] = tamNomeArqv + TAM_CHECKSUM_ARQV; // inseção do número de bytes a serem lidos

	//montagem do CORPO da mensagem
	for (i = 0; i < (tamNomeArqv); i++)
	{
		txMsg[TAM_END_MSG + TAM_HEADER_MSG + TAM_NBYTES + i] = nomeArqv[i]; //inserção do nome do arquivo
	}

	*(CHECKSUM_TYPE*)(txMsg + (TAM_END_MSG + TAM_HEADER_MSG + TAM_NBYTES + i)) = checkSum; //inserção do checksum na mensagem
	//DEBUG
	DEBUGMsg(txMsg, TAM_END_MSG + TAM_HEADER_MSG + TAM_CHECKSUM_ARQV + TAM_NBYTES + i);

	txBytes = TAM_END_MSG + TAM_HEADER_MSG + TAM_CHECKSUM_ARQV + TAM_NBYTES + i; //set message size

	//envio da mensagem
	return Send(commHandler, txMsg, txBytes, &bytes_escritos, NULL);

}
//INSERIR O BIT DE VALIDADE NO HEADER
//return 0 erro , return 1 ok
int FOTA_OperationSync(HANDLE commHandler, int nNo,const int op)
{
	DWORD bytesEscritos;
	int i;

	//log
	fprintf(stderr, ">>>> Realizando operacao: %i <<<<\n", op);
	fprintf(stderr, "Montando Mensagem...\n");

	txMsg[0] = nNo; //montagem do endereçamento da mensagem

	alternateBit = 0; //reset alternate bit

	txMsg[TAM_END_MSG] = op | (alternateBit ? ALTERNATE_BIT_MASK : 0); //montagem do HEADER da mensagem

	//alternateBit ^= 1; //alterna o bit

	//DEBUG
	DEBUGMsg(txMsg, TAM_END_MSG + TAM_HEADER_MSG);

	txBytes = TAM_END_MSG + TAM_HEADER_MSG; //set message size

	//envio da mensagem
	return Send(commHandler, txMsg, txBytes, &bytesEscritos, NULL);

}

int FOTA_Sync(HANDLE commHandler, int nNo)
{
	return FOTA_OperationSync(commHandler, nNo,OP_SYNC);
}
//return 0 erro , return 1 ok
int FOTA_CriarArqv(HANDLE commHandler, int nNo, char nomeArqv[])
{
	return FOTA_Operation(commHandler, nNo, nomeArqv, strlen(nomeArqv), OP_CRIAR);
}
//return 0 erro , return 1 ok
int FOTA_Programar(HANDLE commHandler, int nNo, char nomeArqv[])
{
	return FOTA_Operation(commHandler, nNo, nomeArqv, strlen(nomeArqv), OP_PROGRAMAR);
}
//return 0 erro , return 1 ok
int FOTA_ApagarArqv(HANDLE commHandler, int nNo, char nomeArqv[])
{
	return FOTA_Operation(commHandler, nNo, nomeArqv, strlen(nomeArqv), OP_APAGAR);
}
//return 0 erro , return 1 ok
int FOTA_VerificarArqv(HANDLE commHandler, int nNo, char nomeArqv[])
{
	unsigned int checkSum;

	if ((checkSum = CalcularCheckSumArqv(nomeArqv)) == -1)
	{
		//Falha durante o calculo do Checksum
		return 0;	
	}
	
	return FOTA_OperationVerificar(commHandler, nNo, nomeArqv, strlen(nomeArqv), OP_VERIFICAR, checkSum);
}
//return 0 erro , return 1 ok
int FOTA_EnviarPac(HANDLE commHandler, int nNo, byte pacote[], int numBytesPacote)
{
	DWORD bytesEscritos;
	int i;

	fprintf(stderr, ">>>> Realizando operacao: %i <<<<\n", OP_ENVIAR_PAC);
	fprintf(stderr, "Montando Mensagem...\n");

	txMsg[0] = nNo; //montagem do endereçamento da mensagem

	//montagem do HEADER da mensagens
	txMsg[1] = numBytesPacote; //inserção  do valor do nº de bytes que devem ser lidos 
	txMsg[1] = txMsg[1] << 1; //corrige a posição do nº de bytes que devem ser lidos
	txMsg[1] = txMsg[1] | OP_ENVIAR_PAC;  //inserção do opcode
	txMsg[1] = txMsg[1] | (alternateBit ? ALTERNATE_BIT_MASK : 0); //inserção do bit de controle (ALTERNATE BIT)

	//alternateBit ^= 1; //swap alternate bit
	
	//montagem do CORPO da mensagem
	for (i = 0; i <= numBytesPacote; i++)
	{
		txMsg[TAM_END_MSG + TAM_HEADER_MSG + i] = pacote[i]; //inserção do pacote na mensagem
	}
	
	DEBUGMsg(txMsg, TAM_END_MSG + TAM_HEADER_MSG + i - 1);

	txBytes = TAM_END_MSG + TAM_HEADER_MSG + i - 1; // set message size

	//envio da mensagem
	return Send(commHandler, txMsg, txBytes, &bytesEscritos, NULL);
}

//return -1 erro , return checksum ok
unsigned int CalcularCheckSumArqv(char nomeArqv[])
{
	FILE *pArquivo;
	unsigned int checkSum = 0;
	char aux;

	//log
	printf("Calculando Checksum do Arquivo...\n");

	pArquivo = fopen(nomeArqv, "rb");

	while ((aux = fgetc(pArquivo)) != EOF)
	{

		//verifica se houve algum erro durante a leitura
		if (ferror(pArquivo))
		{
			printf("Erro Durante a Leitura do Arquivo para Calculo do Checksum...");
			return -1;
		}

		checkSum = checkSum + aux;
	}

	printf("CHECKSUM: %i...\n", checkSum);

	fclose(pArquivo);

	return checkSum;
}

//IMPLEMENTAR TIMEOUT
//return 0 - erro , return 1 sucesso , return -1 timeout 
int EsperarResposta(HANDLE commHandler, byte* buffer, int tamBuffer)
{
	DWORD numBytesLidos;
	int count;
	clock_t timer;

	timer = clock();

	fprintf(stderr, "Esperando Resposta...\n");
	//Espera Resposta
		count = 0;
		while (count < tamBuffer) {

			//Lê 1 byte
			if (Receive(commHandler, &buffer[count], 1, &numBytesLidos, NULL))
			{
				count += numBytesLidos;
				if (CheckTimeOut(timer, ANSWER_TIMEOUT))
				{
					return -1;
					
				}
			}
			else
			{
				return 0;
			}

		}

		//DEBUG
		DEBUGMsg(buffer, tamBuffer);

		return 1;
}

int TratarResposta(byte* resposta)
{
	//DEBUG
	printf("Alternate bit: %d | %d\n",((resposta[TAM_END_MSG] & ALTERNATE_BIT_MASK) >> 7),alternateBit);

	if (((resposta[TAM_END_MSG] & ALTERNATE_BIT_MASK) >> 7) != alternateBit)
	{
		fprintf(stderr, "Alternate bit error\n");
		return 0;
	}
	else
	{
		alternateBit ^= 1; //swap alternate bit
		if ((resposta[TAM_END_MSG] & ERROR_MASK) != OK)
		{
			//Start secondary execution flux
			VerifyError(resposta[TAM_END_MSG]);
			fprintf(stderr, "Press any key to continue\n");
			getchar();
			return 0;
		}
		else
		{
			//Continue execution
			fprintf(stderr, "Operation Successful\n");
			return 1;
		}
	}
		
}

void VerifyError(byte header)
{
	byte op, error;
	fprintf(stderr, "Error: ");

	op = header & OP_MASK;
	error = header & ERROR_MASK;

	if (op == OP_ENVIAR_PAC)
	{
		fprintf(stderr, "OP = Write Data | ");
		if (error == ERROR_WRITE_DATA_INVALID_OPERATION) fprintf(stderr, "ERROR[%x] = Invalid operation\n", error >> 4); else
			if (error == ERROR_WRITE_DATA_OPENING_NOT_POSSIBLE) fprintf(stderr, "ERROR[%x] = File could not be open\n", error >> 4); else
				if (error == ERROR_WRITE_DATA_ALL_DATA_NOT_WRITTEN) fprintf(stderr, "ERROR[%x] = All message data not written\n", error >> 4);

	}
	else
	{
		if (op == OP_CRIAR)
		{
			fprintf(stderr, "OP = Create | ");
			if (error == ERROR_CREATE_FILE_ALREADY_EXISTS) fprintf(stderr, "ERROR[%x] = File already exists\n", error >> 4); else
				if (error == ERROR_CREATE_OPENING_NOT_POSSIBLE) fprintf(stderr, "ERROR[%x] = File could not be create\n", error >> 4);
		}
		else
		{
			if (op == OP_APAGAR)
			{
				fprintf(stderr, "OP = Erase | ");
				if (error == ERROR_ERASE_FILE_DOESNT_EXIST) fprintf(stderr, "ERROR[%x] = File doesn't exists\n", error >> 4); else
					if (error == ERROR_ERASE_ERASING_NOT_POSSIBLE) fprintf(stderr, "ERROR[%x] = Fail to remove file\n", error >> 4);
			}
			else
			{
				if (op == OP_VERIFICAR)
				{
					fprintf(stderr, "OP = Verify | ");
					if (error == ERROR_VERIFY_FILE_DOESNT_EXIST) fprintf(stderr, "ERROR[%x] = File doesn't exists\n", error >> 4); else
						if (error == ERROR_VERIFY_OPENING_NOT_POSSIBLE) fprintf(stderr, "ERROR[%x] = File could not be open\n", error >> 4); else
							if (error == ERROR_VERIFY_INCORRECT_CHECKSUM) fprintf(stderr, "ERROR[%x] = Incorrect checksum\n", error >> 4);
				}
				else
				{
					if (op == OP_SD_BEGIN)
					{
						fprintf(stderr, "OP = SD Begin | ");
						fprintf(stderr, "ERROR[%x] = SD could not be properly initialized\n", error >> 4);
					}
					else
					{
						if (op == OP_PROGRAMAR)
						{
							fprintf(stderr, "OP = Update | ");
							if (error == ERROR_UPLOAD_INVALID_OPERATION) fprintf(stderr, "ERROR[%x] = Invalid operation\n", error >> 4);
						}
						else
						{
							fprintf(stderr, "Unknown\n");
						}
					}
				}
			}
		}
	}
	fflush(stdout);
}

int CheckTimeOut(clock_t timer, float timeLimit)
{

	if (((float)(clock() - timer) / CLOCKS_PER_SEC) <= timeLimit)
	{
		return 0;
	}
	else
	{
		fprintf(stderr, "Timeout %d...\n",timeoutCounter+1);
		return 1;
	}


}

void Menu(char* filename, int* op)
{
	do {
		printf("Choose one operation\n");
		printf("1) Upload new firmware\n");
		printf("2) Upload already saved firmware\n");
		printf("3) Erase file\n");
		printf("4)Exit\n");
		printf("OP: ");
		scanf("%d", op);
		getchar();
	} while (*op < 1 || *op > 4);

	if (*op != 4)
	{
		printf("(Maximum file name size is 16 characters, considering the extension)\n");
		printf("Write the file name: ");
		fgets(filename, TAM_MAX_NOME_ARQV + 1, stdin);
		filename[strlen(filename)-1] = '\0';

	}
	
}

void AskForAddress(int* addr)
{
	printf("Write the node adress: ");
	scanf("%d", addr);
	getchar();
}

int Sync(HANDLE hSerial, int addr)
{
	byte resposta[TAM_RESP];

	if (!FOTA_Sync(hSerial, addr))
	{
		//Erro no envio da mensagem 
		return 0;
	}

	if (!EsperarResposta(hSerial, resposta, TAM_RESP))
	{
		//Erro na leitura da resposta
		return 0;
	}

	if (!TratarResposta(resposta))
	{
		//Ocorreu algum erro
		return 0;
	}

	return 1;
}

int UploadNewFirmware(HANDLE hSerial, char* nomeArqv, int addr)
{
	FILE *pArquivo;
	pArquivo = fopen(nomeArqv, "rb");
	char bufferDeEnvio[TAM_CORPO_MSG];
	int numBytesMsg;
	byte resposta[TAM_RESP];

	if (!FOTA_CriarArqv(hSerial, addr, nomeArqv))
	{
		//Erro no envio da mensagem 
		return 0;
	}

	if (!ReceiveAnswer(hSerial, resposta))
	{
		return 0;
	}
	

	while (!feof(pArquivo))
	{

		numBytesMsg = fread(bufferDeEnvio, sizeof(char), TAM_CORPO_MSG, pArquivo);

		//verifica se houve algum erro durante a leitura
		if (ferror(pArquivo))
		{
			printf("Erro durante a leitura do arquivo");
			return 0;

		}
		else
		{
			if (!FOTA_EnviarPac(hSerial, addr, (byte*)bufferDeEnvio, numBytesMsg))
			{
				return 0;
			}

			if (!ReceiveAnswer(hSerial, resposta))
			{
				return 0;
			}
		}

	}

	if (!FOTA_VerificarArqv(hSerial, addr, nomeArqv))
	{
		return 0;
	}

	if (!ReceiveAnswer(hSerial, resposta))
	{
		return 0;
	}

	if (!FOTA_Programar(hSerial, addr, nomeArqv))
	{
		return 0;
	}

	if (!ReceiveAnswer(hSerial, resposta))
	{
		return 0;
	}

	fprintf(stderr, "Upload realizado...\n");
	fprintf(stderr, "Pressione qualquer tecla para finalizar o programa.\n");

	return 1;
}

int UploadSavedFirmware(HANDLE hSerial, char* nomeArqv, int addr)
{

	byte resposta[TAM_RESP];

	if (!FOTA_VerificarArqv(hSerial, addr, nomeArqv))
	{
		return 0;
	}

	if (!EsperarResposta(hSerial, resposta, TAM_RESP))
	{
		return 0;
	}

	if (!TratarResposta(resposta))
	{
		//Ocorreu algum erro
		return 0;
	}


	if (!FOTA_Programar(hSerial, addr, nomeArqv))
	{
		return 0;
	}

	if (!EsperarResposta(hSerial, resposta, TAM_RESP))
	{
		return 0;
	}

	if (!TratarResposta(resposta))
	{
		//Ocorreu algum erro
		return 0;
	}

	fprintf(stderr, "Upload realizado...\n");
	fprintf(stderr, "Pressione qualquer tecla para finalizar o programa.\n");

	return 1;
}

int EraseFile(HANDLE hSerial, char* nomeArqv, int addr)
{

	byte resposta[TAM_RESP];

	if (!FOTA_ApagarArqv(hSerial, addr, nomeArqv))
	{
		return 0;
	}

	if (!ReceiveAnswer(hSerial, resposta))
	{
		return 0;
	}

	return 1;
}

//return 0 - erro, return 1 success
int ReceiveAnswer(HANDLE hSerial, byte* resposta)
{
	int result;

	timeoutCounter = 0;
	do
	{
		result = EsperarResposta(hSerial, resposta, TAM_RESP);

		if (result == -1)
		{
			//timeout
			if (timeoutCounter >= MAX_TIMEOUTS-1)
			{
				fprintf(stderr, "Maximum timeouts...\n");
				timeoutCounter = 0;
				return 0;
			}
			timeoutCounter++;
			ResendLastMessage(hSerial);
		}
		else
		{
			if (result == 0)
			{
				//Error
				return 0;
			}
		}

	} while (result == -1);

	if (!TratarResposta(resposta))
	{
		//Ocorreu algum erro
		return 0;
	}
}