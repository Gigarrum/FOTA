# FOTA
Projeto de iniciação científica - upload de firmware over the air (FOTA) 2016 - 2018

Resumo

A atualização remota de firmware é uma das funcionalidades mais importantes em uma rede sensor sem fio (RSSF) dado que muitos nós sensores encontram-se em locais remotos e de difícil acesso. Atualmente a atualização de firmware de muitos nós sensores ainda é realizada via conexão local. Esse artigo apresenta uma proposta de implantação da funcionalidade de atualização remota, denominada firmware over the air (FOTA), sob uma RSSF. Como benefícios dessa pesquisa será uma arquitetura de implantação que permita a programação remota de um nó sensor, um protocolo para permitir a comunicação entre os dispositivos dessa arquitetura e todas as aplicações necessárias para colocar em prática essa arquitetura.

<h1>Updates</h1>
 
 15/02/2019
  -> Processo de unificação dos códigos do programador do nó sensor e do nó sensor em apenas um chamado de ROTA_Firmware. Dentre os arquivos criados para esse processo,existe o arquivo de configuração FOTA_config.h, responsável por definir diretivas que diferenciam os códigos gerados durante a compilação, adaptando-o para upload tanto para o programador quanto para o nó sensor. Importante ressaltar que esse procedimento de união dos códigos ainda não foi finalizado.
  
